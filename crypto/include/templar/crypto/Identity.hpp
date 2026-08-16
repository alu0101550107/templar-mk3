#pragma once

#include <sodium.h>

#include <array>
#include <cstdint>
#include <vector>

namespace templar::crypto {

using Bytes = std::vector<uint8_t>;
using DhOutput = std::array<unsigned char, crypto_scalarmult_BYTES>;

// Llama a sodium_init(). Es idempotente y segura de invocar varias veces,
// pero debe haberse llamado al menos una vez antes de lanzar hilos que usen
// libsodium. Se llama también desde el constructor de Identity por comodidad.
void initSodium();

struct X25519KeyPair {
  unsigned char pk[crypto_box_PUBLICKEYBYTES]{};
  unsigned char sk[crypto_box_SECRETKEYBYTES]{};
};

struct Ed25519KeyPair {
  unsigned char pk[crypto_sign_PUBLICKEYBYTES]{};
  unsigned char sk[crypto_sign_SECRETKEYBYTES]{};
};

X25519KeyPair generateX25519KeyPair();
Ed25519KeyPair generateEd25519KeyPair();

// Identidad criptográfica de un usuario: un par de firma (Ed25519, autentica
// las prekeys) y un par de intercambio de claves (X25519, usado en las DH de
// X3DH). Se generan una única vez en el registro; las claves privadas nunca
// deben salir del cliente ni tocar disco sin cifrar.
class Identity {
 public:
  Identity();

  // Restaura una identidad ya existente (cargada de un almacen local
  // cifrado) en vez de generar una nueva. Las claves ya deben venir
  // generadas -- este constructor no valida que ed/x formen un par
  // matematicamente consistente, solo las adopta tal cual.
  Identity(const Ed25519KeyPair& ed, const X25519KeyPair& x) : ed_(ed), x_(x) {}

  const Ed25519KeyPair& signingKeys() const { return ed_; }
  const X25519KeyPair& exchangeKeys() const { return x_; }

  Bytes sign(const Bytes& message) const;

 private:
  Ed25519KeyPair ed_;
  X25519KeyPair x_;
};

bool verify(const unsigned char signerPk[crypto_sign_PUBLICKEYBYTES], const Bytes& message,
            const Bytes& signature);

// DH crudo (Curve25519) entre una clave privada propia y una publica ajena.
// Usado tanto por X3DH como por el Double Ratchet -- una unica
// implementacion evita que las dos diverjan en algo tan sensible. Lanza si
// el resultado es el elemento identidad (punto de orden bajo, posible par
// malicioso); libsodium ya rechaza esto internamente en crypto_scalarmult.
DhOutput diffieHellman(const unsigned char sk[crypto_box_SECRETKEYBYTES],
                       const unsigned char pk[crypto_box_PUBLICKEYBYTES]);

}  // namespace templar::crypto
