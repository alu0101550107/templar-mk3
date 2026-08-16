#include "CryptoEngine.hpp"

#include <cstring>
#include <stdexcept>

namespace templar::client {

using namespace templar::crypto;

namespace {

constexpr size_t kRatchetHeaderLen = crypto_box_PUBLICKEYBYTES + 8;

// El servidor solo ve un blob de "ciphertext" opaco (ver Router.cpp) --
// dentro de ese blob empaquetamos nosotros la cabecera del ratchet (clave DH
// actual + PN + N) seguida del ciphertext AEAD real. Esto evita tocar el
// protocolo de red para nada relacionado con el ratchet.
Bytes packRatchetMessage(const RatchetMessage& msg) {
  Bytes out = msg.header.serialize();
  out.insert(out.end(), msg.ciphertext.begin(), msg.ciphertext.end());
  return out;
}

RatchetMessage unpackRatchetMessage(const Bytes& wire) {
  if (wire.size() < kRatchetHeaderLen) {
    throw std::runtime_error("Mensaje de ratchet demasiado corto (cabecera incompleta)");
  }
  Bytes headerBytes(wire.begin(), wire.begin() + kRatchetHeaderLen);
  RatchetMessage msg;
  msg.header = RatchetHeader::parse(headerBytes);
  msg.ciphertext.assign(wire.begin() + kRatchetHeaderLen, wire.end());
  return msg;
}

// AD = IK_X25519 del iniciador de X3DH || IK_X25519 del receptor, en ese
// orden fijo en ambos lados -- quien manda el primer mensaje siempre hizo de
// iniciador, asi que ambos calculan exactamente los mismos bytes.
Bytes buildAssociatedData(const unsigned char initiatorIk[crypto_box_PUBLICKEYBYTES],
                          const unsigned char responderIk[crypto_box_PUBLICKEYBYTES]) {
  Bytes ad;
  ad.insert(ad.end(), initiatorIk, initiatorIk + crypto_box_PUBLICKEYBYTES);
  ad.insert(ad.end(), responderIk, responderIk + crypto_box_PUBLICKEYBYTES);
  return ad;
}

}  // namespace

CryptoEngine::CryptoEngine() {
  signedPrekey_ = generateX25519KeyPair();
  Bytes spkBytes(signedPrekey_.pk, signedPrekey_.pk + crypto_box_PUBLICKEYBYTES);
  signedPrekeySig_ = identity_.sign(spkBytes);
}

void CryptoEngine::restoreIdentity(const Identity& identity, const X25519KeyPair& signedPrekey,
                                   const Bytes& signedPrekeySig) {
  identity_ = identity;
  signedPrekey_ = signedPrekey;
  signedPrekeySig_ = signedPrekeySig;
}

bool CryptoEngine::hasSession(const std::string& peer) const {
  return sessions_.find(peer) != sessions_.end();
}

Bytes CryptoEngine::exportSession(const std::string& peer) const {
  auto it = sessions_.find(peer);
  if (it == sessions_.end()) {
    throw std::runtime_error("No hay sesion establecida con '" + peer + "' para exportar");
  }
  return it->second.serialize();
}

void CryptoEngine::importSession(const std::string& peer, const Bytes& serializedRatchet) {
  sessions_.erase(peer);
  sessions_.emplace(peer, DoubleRatchet::deserialize(serializedRatchet));
}

OutboundFirstMessage CryptoEngine::encryptFirst(const std::string& peer,
                                                const PrekeyBundle& bundle,
                                                const Bytes& plaintext) {
  InitiatorResult result = x3dhInitiate(identity_, bundle);

  const auto& myEx = identity_.exchangeKeys();
  Bytes ad = buildAssociatedData(myEx.pk, bundle.identityPkX25519);

  DoubleRatchet ratchet = DoubleRatchet::initAsSender(result.sharedSecret, result.ephemeral,
                                                      bundle.signedPrekeyPub, ad);

  sessions_.erase(peer);
  auto it = sessions_.emplace(peer, std::move(ratchet)).first;

  RatchetMessage msg = it->second.encrypt(plaintext);

  OutboundFirstMessage out;
  out.senderIdentityPkX25519 = Bytes(myEx.pk, myEx.pk + crypto_box_PUBLICKEYBYTES);
  out.senderEphemeralPk =
      Bytes(result.ephemeral.pk, result.ephemeral.pk + crypto_box_PUBLICKEYBYTES);
  out.ciphertext = packRatchetMessage(msg);
  return out;
}

Bytes CryptoEngine::encryptNext(const std::string& peer, const Bytes& plaintext) {
  auto it = sessions_.find(peer);
  if (it == sessions_.end()) {
    throw std::runtime_error("No hay sesion establecida con '" + peer + "'");
  }
  RatchetMessage msg = it->second.encrypt(plaintext);
  return packRatchetMessage(msg);
}

Bytes CryptoEngine::decryptFirst(const std::string& peer, const Bytes& senderIdentityPkX25519,
                                 const Bytes& senderEphemeralPk, const Bytes& ciphertext,
                                 const X25519KeyPair* usedOneTimePrekey) {
  if (senderIdentityPkX25519.size() != crypto_box_PUBLICKEYBYTES ||
      senderEphemeralPk.size() != crypto_box_PUBLICKEYBYTES) {
    throw std::runtime_error("Cabecera X3DH invalida en el primer mensaje de '" + peer + "'");
  }

  unsigned char theirIdPk[crypto_box_PUBLICKEYBYTES];
  unsigned char theirEphPk[crypto_box_PUBLICKEYBYTES];
  std::memcpy(theirIdPk, senderIdentityPkX25519.data(), crypto_box_PUBLICKEYBYTES);
  std::memcpy(theirEphPk, senderEphemeralPk.data(), crypto_box_PUBLICKEYBYTES);

  // Si A tenia una one-time prekey nuestra disponible, usedOneTimePrekey ya
  // trae la pareja completa (localizada por quien llama a partir de la
  // clave publica que vino en el mensaje) y X3DH opera con las 4 DH
  // completas. Si no, cae al modo de 3-DH (IK + SPK) -- sigue siendo
  // seguro, solo pierde el margen extra que da la 4a DH.
  SharedSecret sk =
      x3dhRespond(identity_, signedPrekey_, usedOneTimePrekey, theirIdPk, theirEphPk);

  // Quien nos escribe primero es siempre el iniciador de X3DH: AD = su IK ||
  // la nuestra (mismo orden que calcula encryptFirst del otro lado).
  Bytes ad = buildAssociatedData(theirIdPk, identity_.exchangeKeys().pk);

  DoubleRatchet ratchet = DoubleRatchet::initAsReceiver(sk, signedPrekey_, ad);

  sessions_.erase(peer);
  auto it = sessions_.emplace(peer, std::move(ratchet)).first;

  RatchetMessage msg = unpackRatchetMessage(ciphertext);
  return it->second.decrypt(msg);
}

Bytes CryptoEngine::decryptNext(const std::string& peer, const Bytes& ciphertext) {
  auto it = sessions_.find(peer);
  if (it == sessions_.end()) {
    throw std::runtime_error("Mensaje de sesion desconocida de '" + peer + "'");
  }
  RatchetMessage msg = unpackRatchetMessage(ciphertext);
  return it->second.decrypt(msg);
}

}  // namespace templar::client
