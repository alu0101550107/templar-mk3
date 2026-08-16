#include "templar/crypto/X3DH.hpp"

#include <cstring>
#include <stdexcept>

namespace templar::crypto {

namespace {

// HKDF-SHA256(salt=0^32, IKM) -> Expand con info fija -> 32 bytes de SK.
// El prefijo de 0xFF*32 antes del IKM sigue la especificacion de X3DH de
// Signal: separa este uso de otros protocolos que pudieran operar sobre las
// mismas claves Curve25519.
SharedSecret deriveSharedSecret(const std::vector<DhOutput>& dhOutputs) {
  Bytes ikm(32, 0xFF);
  for (const auto& d : dhOutputs) {
    ikm.insert(ikm.end(), d.begin(), d.end());
  }

  unsigned char salt[crypto_kdf_hkdf_sha256_KEYBYTES] = {0};
  unsigned char prk[crypto_kdf_hkdf_sha256_KEYBYTES];
  crypto_kdf_hkdf_sha256_extract(prk, salt, sizeof(salt), ikm.data(), ikm.size());

  static const char kInfo[] = "Templar_X3DH_v1";
  SharedSecret sk;
  crypto_kdf_hkdf_sha256_expand(sk.data(), sk.size(), kInfo, sizeof(kInfo) - 1, prk);

  sodium_memzero(prk, sizeof(prk));
  sodium_memzero(ikm.data(), ikm.size());
  return sk;
}

}  // namespace

bool PrekeyBundle::verify() const {
  Bytes msg(signedPrekeyPub, signedPrekeyPub + crypto_box_PUBLICKEYBYTES);
  return templar::crypto::verify(identityPkEd25519, msg, signedPrekeySig);
}

InitiatorResult x3dhInitiate(const Identity& myIdentity, const PrekeyBundle& theirBundle) {
  if (!theirBundle.verify()) {
    throw std::runtime_error("X3DH: firma de la signed prekey invalida (bundle no confiable)");
  }

  X25519KeyPair ek = generateX25519KeyPair();  // EK_A, efimera para esta sesion

  const auto& myX = myIdentity.exchangeKeys();

  // DH1 = DH(IK_A, SPK_B)   DH2 = DH(EK_A, IK_B)
  // DH3 = DH(EK_A, SPK_B)   DH4 = DH(EK_A, OPK_B)  [si hay one-time prekey]
  std::vector<DhOutput> outs;
  outs.push_back(diffieHellman(myX.sk, theirBundle.signedPrekeyPub));
  outs.push_back(diffieHellman(ek.sk, theirBundle.identityPkX25519));
  outs.push_back(diffieHellman(ek.sk, theirBundle.signedPrekeyPub));
  if (theirBundle.hasOneTimePrekey) {
    outs.push_back(diffieHellman(ek.sk, theirBundle.oneTimePrekeyPub));
  }

  InitiatorResult result;
  result.sharedSecret = deriveSharedSecret(outs);
  result.ephemeral = ek;
  return result;
}

SharedSecret x3dhRespond(const Identity& myIdentity, const X25519KeyPair& mySignedPrekey,
                         const X25519KeyPair* myOneTimePrekey,
                         const unsigned char theirIdentityPkX25519[crypto_box_PUBLICKEYBYTES],
                         const unsigned char theirEphemeralPk[crypto_box_PUBLICKEYBYTES]) {
  const auto& myX = myIdentity.exchangeKeys();

  // Mismas cuatro DH que el iniciador, pero calculadas desde el otro lado:
  // DH1 = DH(SPK_B, IK_A)   DH2 = DH(IK_B, EK_A)
  // DH3 = DH(SPK_B, EK_A)   DH4 = DH(OPK_B, EK_A)
  std::vector<DhOutput> outs;
  outs.push_back(diffieHellman(mySignedPrekey.sk, theirIdentityPkX25519));
  outs.push_back(diffieHellman(myX.sk, theirEphemeralPk));
  outs.push_back(diffieHellman(mySignedPrekey.sk, theirEphemeralPk));
  if (myOneTimePrekey != nullptr) {
    outs.push_back(diffieHellman(myOneTimePrekey->sk, theirEphemeralPk));
  }

  return deriveSharedSecret(outs);
}

}  // namespace templar::crypto
