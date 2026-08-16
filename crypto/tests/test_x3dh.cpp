// Tests de humo para X3DH. No es un framework de verdad (sin gtest para no
// meter otra dependencia en esta fase): cada check lanza std::runtime_error
// si falla y main() reporta el resultado. Los tests del cifrado en si
// (Double Ratchet) estan en test_double_ratchet.cpp.

#include <cstring>
#include <iostream>
#include <stdexcept>

#include "templar/crypto/Identity.hpp"
#include "templar/crypto/X3DH.hpp"

using namespace templar::crypto;

namespace {

void check(bool cond, const char* what) {
  if (!cond) throw std::runtime_error(std::string("FALLO: ") + what);
}

// Construye el bundle que Bob publicaría en el servidor.
struct BobMaterial {
  Identity identity;
  X25519KeyPair signedPrekey;
  X25519KeyPair oneTimePrekey;
  PrekeyBundle bundle;
};

BobMaterial makeBobMaterial(bool includeOtpk) {
  BobMaterial bob;
  bob.signedPrekey = generateX25519KeyPair();
  bob.oneTimePrekey = generateX25519KeyPair();

  Bytes spkBytes(bob.signedPrekey.pk, bob.signedPrekey.pk + crypto_box_PUBLICKEYBYTES);
  Bytes sig = bob.identity.sign(spkBytes);

  std::memcpy(bob.bundle.identityPkX25519, bob.identity.exchangeKeys().pk,
              crypto_box_PUBLICKEYBYTES);
  std::memcpy(bob.bundle.identityPkEd25519, bob.identity.signingKeys().pk,
              crypto_sign_PUBLICKEYBYTES);
  std::memcpy(bob.bundle.signedPrekeyPub, bob.signedPrekey.pk, crypto_box_PUBLICKEYBYTES);
  bob.bundle.signedPrekeySig = sig;
  bob.bundle.hasOneTimePrekey = includeOtpk;
  if (includeOtpk) {
    std::memcpy(bob.bundle.oneTimePrekeyPub, bob.oneTimePrekey.pk, crypto_box_PUBLICKEYBYTES);
  }
  return bob;
}

void testBundleVerification() {
  BobMaterial bob = makeBobMaterial(true);
  check(bob.bundle.verify(), "bundle legitimo deberia verificar OK");

  // Un servidor (u otro atacante) que sustituya la signed prekey por la suya
  // propia debe ser detectado: la firma ya no coincide.
  PrekeyBundle tampered = bob.bundle;
  X25519KeyPair attackerKey = generateX25519KeyPair();
  std::memcpy(tampered.signedPrekeyPub, attackerKey.pk, crypto_box_PUBLICKEYBYTES);
  check(!tampered.verify(), "bundle con signed prekey sustituida debe fallar la verificacion");
}

void testX3dhAgreementWithOtpk() {
  Identity alice;
  BobMaterial bob = makeBobMaterial(/*includeOtpk=*/true);

  InitiatorResult aliceSide = x3dhInitiate(alice, bob.bundle);

  SharedSecret bobSide =
      x3dhRespond(bob.identity, bob.signedPrekey, &bob.oneTimePrekey,
                  alice.exchangeKeys().pk, aliceSide.ephemeral.pk);

  check(aliceSide.sharedSecret == bobSide,
        "ambos lados deben derivar la misma SK con one-time prekey");
}

void testX3dhAgreementWithoutOtpk() {
  Identity alice;
  BobMaterial bob = makeBobMaterial(/*includeOtpk=*/false);

  InitiatorResult aliceSide = x3dhInitiate(alice, bob.bundle);

  SharedSecret bobSide =
      x3dhRespond(bob.identity, bob.signedPrekey, /*myOneTimePrekey=*/nullptr,
                  alice.exchangeKeys().pk, aliceSide.ephemeral.pk);

  check(aliceSide.sharedSecret == bobSide,
        "ambos lados deben derivar la misma SK sin one-time prekey (DH1-DH3 solamente)");
}

void testX3dhRejectsInvalidBundle() {
  Identity alice;
  BobMaterial bob = makeBobMaterial(true);
  bob.bundle.signedPrekeySig[0] ^= 0xFF;  // corrompe la firma

  bool threw = false;
  try {
    x3dhInitiate(alice, bob.bundle);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "x3dhInitiate debe rechazar un bundle con firma invalida");
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    void (*fn)();
  };

  TestCase tests[] = {
      {"bundle_verification", testBundleVerification},
      {"x3dh_agreement_with_otpk", testX3dhAgreementWithOtpk},
      {"x3dh_agreement_without_otpk", testX3dhAgreementWithoutOtpk},
      {"x3dh_rejects_invalid_bundle", testX3dhRejectsInvalidBundle},
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
