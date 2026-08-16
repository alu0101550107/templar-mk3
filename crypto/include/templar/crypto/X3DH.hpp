#pragma once

#include <array>

#include "templar/crypto/Identity.hpp"

namespace templar::crypto {

constexpr size_t kSharedSecretLen = 32;
using SharedSecret = std::array<unsigned char, kSharedSecretLen>;

// Lo que el servidor entrega cuando alguien quiere iniciar conversación con
// un usuario que puede estar desconectado ("prekey bundle" en la jerga de
// Signal). signedPrekeySig demuestra que quien controla identityPkEd25519
// generó realmente signedPrekeyPub (evita que el servidor, u otro atacante
// en medio, sustituya la prekey por una suya).
struct PrekeyBundle {
  unsigned char identityPkX25519[crypto_box_PUBLICKEYBYTES]{};
  unsigned char identityPkEd25519[crypto_sign_PUBLICKEYBYTES]{};
  unsigned char signedPrekeyPub[crypto_box_PUBLICKEYBYTES]{};
  Bytes signedPrekeySig;
  bool hasOneTimePrekey = false;
  unsigned char oneTimePrekeyPub[crypto_box_PUBLICKEYBYTES]{};

  // Verifica signedPrekeySig contra identityPkEd25519. SIEMPRE debe llamarse
  // antes de usar el bundle en x3dhInitiate: un bundle sin verificar permite
  // que un servidor malicioso monte un ataque de intermediario.
  bool verify() const;
};

struct InitiatorResult {
  SharedSecret sharedSecret;
  X25519KeyPair ephemeral;  // EK_A: hay que enviar ephemeral.pk al receptor en el primer mensaje
};

// Lado iniciador (A): calcula DH1..DH4 contra el bundle de B y deriva SK
// mediante HKDF-SHA256. Lanza std::runtime_error si bundle.verify() falla o
// si alguna operación DH produce un punto de orden bajo (par malicioso).
InitiatorResult x3dhInitiate(const Identity& myIdentity, const PrekeyBundle& theirBundle);

// Lado receptor (B): repite las mismas cuatro DH usando su clave privada de
// signed prekey (y la one-time prekey que se consumió, si hubo) más las
// claves públicas de A que llegaron en el primer mensaje cifrado.
SharedSecret x3dhRespond(const Identity& myIdentity, const X25519KeyPair& mySignedPrekey,
                         const X25519KeyPair* myOneTimePrekey,
                         const unsigned char theirIdentityPkX25519[crypto_box_PUBLICKEYBYTES],
                         const unsigned char theirEphemeralPk[crypto_box_PUBLICKEYBYTES]);

}  // namespace templar::crypto
