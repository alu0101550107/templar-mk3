// Tests de humo para el Double Ratchet. Igual que test_x3dh.cpp: sin
// framework externo, cada check() lanza si falla y main() reporta.

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "templar/crypto/DoubleRatchet.hpp"
#include "templar/crypto/Identity.hpp"
#include "templar/crypto/X3DH.hpp"

using namespace templar::crypto;

namespace {

void check(bool cond, const char* what) {
  if (!cond) throw std::runtime_error(std::string("FALLO: ") + what);
}

// encrypt/decrypt trabajan con Bytes crudos (no solo texto) desde que el
// ratchet tambien se usa para fragmentos binarios de archivos -- este
// helper deja los tests igual de legibles que cuando tomaban std::string.
Bytes B(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string S(const Bytes& b) { return std::string(b.begin(), b.end()); }

struct Pair {
  DoubleRatchet alice;
  DoubleRatchet bob;
};

// Reproduce el arranque real (X3DH entre A y B, luego Double Ratchet
// inicializado con la SK resultante) -- el mismo camino que usa
// CryptoEngine en el cliente.
Pair makeEstablishedPair() {
  Identity aliceId;
  Identity bobId;
  X25519KeyPair bobSignedPrekey = generateX25519KeyPair();

  Bytes spkBytes(bobSignedPrekey.pk, bobSignedPrekey.pk + crypto_box_PUBLICKEYBYTES);
  Bytes spkSig = bobId.sign(spkBytes);

  PrekeyBundle bundle{};
  std::memcpy(bundle.identityPkX25519, bobId.exchangeKeys().pk, crypto_box_PUBLICKEYBYTES);
  std::memcpy(bundle.identityPkEd25519, bobId.signingKeys().pk, crypto_sign_PUBLICKEYBYTES);
  std::memcpy(bundle.signedPrekeyPub, bobSignedPrekey.pk, crypto_box_PUBLICKEYBYTES);
  bundle.signedPrekeySig = spkSig;

  InitiatorResult aliceX3dh = x3dhInitiate(aliceId, bundle);
  SharedSecret bobSk = x3dhRespond(bobId, bobSignedPrekey, /*myOneTimePrekey=*/nullptr,
                                   aliceId.exchangeKeys().pk, aliceX3dh.ephemeral.pk);

  // AD = IK_A || IK_B, en ese orden en ambos lados (Alice es la
  // iniciadora).
  Bytes ad;
  const auto& aliceIkX = aliceId.exchangeKeys().pk;
  const auto& bobIkX = bobId.exchangeKeys().pk;
  ad.insert(ad.end(), aliceIkX, aliceIkX + crypto_box_PUBLICKEYBYTES);
  ad.insert(ad.end(), bobIkX, bobIkX + crypto_box_PUBLICKEYBYTES);

  DoubleRatchet alice = DoubleRatchet::initAsSender(aliceX3dh.sharedSecret, aliceX3dh.ephemeral,
                                                    bundle.signedPrekeyPub, ad);
  DoubleRatchet bob = DoubleRatchet::initAsReceiver(bobSk, bobSignedPrekey, ad);

  return Pair{std::move(alice), std::move(bob)};
}

void testBasicRoundtrip() {
  Pair p = makeEstablishedPair();

  RatchetMessage m1 = p.alice.encrypt(B("hola bob"));
  check(S(p.bob.decrypt(m1)) == "hola bob", "primer mensaje debe descifrar correctamente");

  RatchetMessage m2 = p.bob.encrypt(B("hola alice"));
  check(S(p.alice.decrypt(m2)) == "hola alice",
       "respuesta de bob debe descifrar correctamente (dispara el ratchet de alice)");
}

void testSequentialChain() {
  Pair p = makeEstablishedPair();
  p.bob.decrypt(p.alice.encrypt(B("init")));

  for (int i = 0; i < 5; ++i) {
    std::string text = "mensaje numero " + std::to_string(i);
    check(S(p.bob.decrypt(p.alice.encrypt(B(text)))) == text,
         "mensajes secuenciales en la misma cadena deben descifrar en orden");
  }
}

void testOutOfOrderDelivery() {
  Pair p = makeEstablishedPair();
  p.bob.decrypt(p.alice.encrypt(B("init")));

  RatchetMessage m1 = p.alice.encrypt(B("uno"));
  RatchetMessage m2 = p.alice.encrypt(B("dos"));
  RatchetMessage m3 = p.alice.encrypt(B("tres"));

  // Llegan en el orden 3, 1, 2.
  check(S(p.bob.decrypt(m3)) == "tres", "mensaje adelantado debe descifrar y saltar los previos");
  check(S(p.bob.decrypt(m1)) == "uno", "mensaje atrasado debe recuperarse de skippedKeys_");
  check(S(p.bob.decrypt(m2)) == "dos", "otro mensaje atrasado debe recuperarse de skippedKeys_");
}

void testLostMessageNeverArrives() {
  Pair p = makeEstablishedPair();
  p.bob.decrypt(p.alice.encrypt(B("init")));

  RatchetMessage lost = p.alice.encrypt(B("este nunca llega"));
  (void)lost;
  RatchetMessage delivered = p.alice.encrypt(B("este si llega"));

  check(S(p.bob.decrypt(delivered)) == "este si llega",
       "un mensaje nunca entregado no debe bloquear los siguientes");
}

void testMultipleRatchetTurns() {
  Pair p = makeEstablishedPair();

  check(S(p.bob.decrypt(p.alice.encrypt(B("turno 1 A->B")))) == "turno 1 A->B", "turno 1 A->B");
  check(S(p.alice.decrypt(p.bob.encrypt(B("turno 1 B->A")))) == "turno 1 B->A", "turno 1 B->A");
  check(S(p.bob.decrypt(p.alice.encrypt(B("turno 2 A->B")))) == "turno 2 A->B", "turno 2 A->B");
  check(S(p.alice.decrypt(p.bob.encrypt(B("turno 2 B->A")))) == "turno 2 B->A", "turno 2 B->A");

  // Varios mensajes seguidos en un mismo turno antes de que el otro
  // responda -- PN debe reflejar cuantos hubo en la cadena anterior.
  check(S(p.bob.decrypt(p.alice.encrypt(B("A dice x")))) == "A dice x", "multi-mensaje turno 3a");
  check(S(p.bob.decrypt(p.alice.encrypt(B("A dice y")))) == "A dice y", "multi-mensaje turno 3b");
  check(S(p.alice.decrypt(p.bob.encrypt(B("B responde")))) == "B responde", "turno 3 B->A");
}

void testTamperedCiphertextRejected() {
  Pair p = makeEstablishedPair();
  RatchetMessage m = p.alice.encrypt(B("no me toques"));
  m.ciphertext.back() ^= 0xFF;

  bool threw = false;
  try {
    p.bob.decrypt(m);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "decrypt debe rechazar un ciphertext manipulado");
}

void testTamperedHeaderRejected() {
  Pair p = makeEstablishedPair();
  RatchetMessage m = p.alice.encrypt(B("cabecera intacta?"));
  m.header.messageNumber += 1;  // la cabecera es parte del AD autenticado

  bool threw = false;
  try {
    p.bob.decrypt(m);
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, "decrypt debe rechazar si la cabecera fue alterada (el AD ya no coincide)");
}

void testMaxSkipLimitEnforced() {
  Pair p = makeEstablishedPair();
  p.bob.decrypt(p.alice.encrypt(B("init")));

  RatchetMessage farAhead;
  for (int i = 0; i < 1500; ++i) {
    farAhead = p.alice.encrypt(B("relleno " + std::to_string(i)));
  }

  bool threw = false;
  try {
    p.bob.decrypt(farAhead);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "saltar mas de kMaxSkip mensajes de golpe debe rechazarse (proteccion DoS)");
}

void testSerializeRoundtripMidConversation() {
  Pair p = makeEstablishedPair();

  p.bob.decrypt(p.alice.encrypt(B("turno 1")));
  p.alice.decrypt(p.bob.encrypt(B("respuesta 1")));
  p.bob.decrypt(p.alice.encrypt(B("turno 2")));

  // Simula "cerrar la app": serializar el estado de bob y reconstruirlo
  // desde cero, como hara LocalStore al recargar tras un reinicio.
  Bytes snapshot = p.bob.serialize();
  DoubleRatchet bobRestored = DoubleRatchet::deserialize(snapshot);

  check(S(p.alice.decrypt(bobRestored.encrypt(B("turno 3 desde el restaurado")))) ==
           "turno 3 desde el restaurado",
       "bob restaurado debe poder seguir cifrando y que alice lo descifre");

  check(S(bobRestored.decrypt(p.alice.encrypt(B("turno 4 hacia el restaurado")))) ==
           "turno 4 hacia el restaurado",
       "bob restaurado debe poder seguir descifrando mensajes nuevos de alice");
}

void testSerializePreservesSkippedKeys() {
  Pair p = makeEstablishedPair();
  p.bob.decrypt(p.alice.encrypt(B("init")));

  RatchetMessage m1 = p.alice.encrypt(B("uno"));
  RatchetMessage m2 = p.alice.encrypt(B("dos"));
  RatchetMessage m3 = p.alice.encrypt(B("tres"));

  // Bob solo recibe el 3, dejando 1 y 2 pendientes en skippedKeys_.
  check(S(p.bob.decrypt(m3)) == "tres", "bob descifra el adelantado antes de serializar");

  Bytes snapshot = p.bob.serialize();
  DoubleRatchet bobRestored = DoubleRatchet::deserialize(snapshot);

  // Las claves saltadas deben sobrevivir al roundtrip -- si no, esto
  // lanzaria en vez de devolver el texto.
  check(S(bobRestored.decrypt(m1)) == "uno",
       "claves saltadas deben sobrevivir serialize/deserialize (mensaje 1)");
  check(S(bobRestored.decrypt(m2)) == "dos",
       "claves saltadas deben sobrevivir serialize/deserialize (mensaje 2)");
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    void (*fn)();
  };

  TestCase tests[] = {
      {"basic_roundtrip", testBasicRoundtrip},
      {"sequential_chain", testSequentialChain},
      {"out_of_order_delivery", testOutOfOrderDelivery},
      {"lost_message_never_arrives", testLostMessageNeverArrives},
      {"multiple_ratchet_turns", testMultipleRatchetTurns},
      {"tampered_ciphertext_rejected", testTamperedCiphertextRejected},
      {"tampered_header_rejected", testTamperedHeaderRejected},
      {"max_skip_limit_enforced", testMaxSkipLimitEnforced},
      {"serialize_roundtrip_mid_conversation", testSerializeRoundtripMidConversation},
      {"serialize_preserves_skipped_keys", testSerializePreservesSkippedKeys},
  };

  int failures = 0;
  for (const auto& t : tests) {
    try {
      t.fn();
      std::cout << "[OK]   " << t.name << "\n";
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
      ++failures;
    }
  }

  if (failures > 0) {
    std::cout << failures << " test(s) fallaron.\n";
    return 1;
  }
  std::cout << "Todos los tests pasaron.\n";
  return 0;
}
