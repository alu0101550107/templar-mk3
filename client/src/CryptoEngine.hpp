#pragma once

#include <string>
#include <unordered_map>

#include "templar/Wire.hpp"
#include "templar/crypto/DoubleRatchet.hpp"
#include "templar/crypto/Identity.hpp"
#include "templar/crypto/X3DH.hpp"

namespace templar::client {

using templar::proto::Bytes;

struct OutboundFirstMessage {
  Bytes senderIdentityPkX25519;
  Bytes senderEphemeralPk;
  Bytes ciphertext;
};

// Mantiene la identidad criptográfica local y las sesiones ya establecidas
// con cada peer -- cada sesión es un Double Ratchet completo (forward
// secrecy y post-compromise security por mensaje), inicializado con la SK
// que produce X3DH.
//
// Por defecto genera una identidad nueva al construirse (para una cuenta
// recién registrada); restoreIdentity()/restoreSession() permiten cargar
// una identidad y sesiones ya existentes desde LocalStore, para sobrevivir
// a reinicios del cliente.
class CryptoEngine {
 public:
  CryptoEngine();

  const templar::crypto::Identity& identity() const { return identity_; }
  const templar::crypto::X25519KeyPair& signedPrekey() const { return signedPrekey_; }
  const Bytes& signedPrekeySignature() const { return signedPrekeySig_; }

  // Reemplaza la identidad generada por el constructor con una cargada de
  // LocalStore. Debe llamarse antes de cualquier encrypt/decrypt, y nunca
  // despues de haber enviado ya un REGISTER con la identidad generada por
  // defecto (romperia la correspondencia con lo publicado en el servidor).
  void restoreIdentity(const templar::crypto::Identity& identity,
                       const templar::crypto::X25519KeyPair& signedPrekey,
                       const Bytes& signedPrekeySig);

  bool hasSession(const std::string& peer) const;

  // Vuelca el estado completo (claves de cadena, contadores, claves
  // saltadas) de la sesion con `peer` para persistirlo en LocalStore.
  // Lanza si no existe sesion con ese peer.
  Bytes exportSession(const std::string& peer) const;

  // Restaura una sesion previamente exportada (p.ej. cargada de
  // LocalStore al reabrir el cliente). Sobreescribe cualquier sesion
  // existente con ese peer.
  void importSession(const std::string& peer, const Bytes& serializedRatchet);

  // El payload es siempre Bytes crudos, no necesariamente texto -- puede
  // ser un ChatLine de texto o un fragmento de una transferencia de archivo,
  // ambos con el mismo pequeño framing interno (ver MessagePayload). La
  // criptografia no necesita saber cual de los dos es.

  // Lado A: primer mensaje de una conversación nueva. Ejecuta X3DH contra el
  // bundle de B (ya debe estar verificado -- ver PrekeyBundle::verify) y
  // deja la sesión establecida para mensajes siguientes.
  OutboundFirstMessage encryptFirst(const std::string& peer,
                                    const templar::crypto::PrekeyBundle& bundle,
                                    const Bytes& plaintext);

  // Lado A: mensajes siguientes de una sesión ya establecida.
  Bytes encryptNext(const std::string& peer, const Bytes& plaintext);

  // Lado B: primer mensaje recibido de un peer nuevo. Completa X3DH con los
  // datos que mandó A y deja la sesión establecida. `usedOneTimePrekey` es
  // la pareja de claves cuya publica A dijo haber usado (o nullptr si A no
  // tenia ninguna disponible) -- quien llama es responsable de localizarla
  // en LocalStore por su clave publica y borrarla despues de un uso exitoso.
  Bytes decryptFirst(const std::string& peer, const Bytes& senderIdentityPkX25519,
                     const Bytes& senderEphemeralPk, const Bytes& ciphertext,
                     const templar::crypto::X25519KeyPair* usedOneTimePrekey);

  // Lado B: mensajes siguientes de una sesión ya establecida.
  Bytes decryptNext(const std::string& peer, const Bytes& ciphertext);

 private:
  templar::crypto::Identity identity_;
  templar::crypto::X25519KeyPair signedPrekey_;
  Bytes signedPrekeySig_;
  std::unordered_map<std::string, templar::crypto::DoubleRatchet> sessions_;
};

}  // namespace templar::client
