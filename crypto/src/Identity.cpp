#include "templar/crypto/Identity.hpp"

#include <stdexcept>

namespace templar::crypto {

void initSodium() {
  if (sodium_init() < 0) {
    throw std::runtime_error("No se pudo inicializar libsodium");
  }
}

X25519KeyPair generateX25519KeyPair() {
  initSodium();
  X25519KeyPair kp;
  crypto_box_keypair(kp.pk, kp.sk);
  return kp;
}

Ed25519KeyPair generateEd25519KeyPair() {
  initSodium();
  Ed25519KeyPair kp;
  crypto_sign_keypair(kp.pk, kp.sk);
  return kp;
}

Identity::Identity() : ed_(generateEd25519KeyPair()), x_(generateX25519KeyPair()) {}

Bytes Identity::sign(const Bytes& message) const {
  Bytes sig(crypto_sign_BYTES);
  unsigned long long sigLen = 0;
  crypto_sign_detached(sig.data(), &sigLen, message.data(), message.size(), ed_.sk);
  sig.resize(sigLen);
  return sig;
}

bool verify(const unsigned char signerPk[crypto_sign_PUBLICKEYBYTES], const Bytes& message,
            const Bytes& signature) {
  if (signature.size() != crypto_sign_BYTES) return false;
  return crypto_sign_verify_detached(signature.data(), message.data(), message.size(),
                                     signerPk) == 0;
}

DhOutput diffieHellman(const unsigned char sk[crypto_box_SECRETKEYBYTES],
                       const unsigned char pk[crypto_box_PUBLICKEYBYTES]) {
  DhOutput out;
  if (crypto_scalarmult(out.data(), sk, pk) != 0) {
    throw std::runtime_error("DH invalida (posible punto de orden bajo / peer malicioso)");
  }
  return out;
}

}  // namespace templar::crypto
