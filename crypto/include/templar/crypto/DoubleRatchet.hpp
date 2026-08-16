#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "templar/crypto/Identity.hpp"
#include "templar/crypto/X3DH.hpp"

namespace templar::crypto {

// Cabecera que viaja (en claro, pero autenticada como AD del AEAD) junto a
// cada mensaje: la clave publica DH actual del emisor, cuantos mensajes
// tenia la cadena de envio anterior (PN) y el numero de este mensaje dentro
// de la cadena actual (N). Con esto el receptor sabe si debe hacer un nuevo
// paso de DH ratchet y cuantas claves de mensajes saltarse.
struct RatchetHeader {
  unsigned char dhPub[crypto_box_PUBLICKEYBYTES]{};
  uint32_t prevChainLen = 0;
  uint32_t messageNumber = 0;

  Bytes serialize() const;
  static RatchetHeader parse(const Bytes& data);
};

struct RatchetMessage {
  RatchetHeader header;
  Bytes ciphertext;
};

// Implementacion del Double Ratchet de Signal
// (https://signal.org/docs/specifications/doubleratchet/): cada mensaje se
// cifra con una clave distinta derivada de una cadena simetrica (forward
// secrecy por mensaje), y cada vez que el otro lado responde con una nueva
// clave DH efimera, la raiz compartida vuelve a mezclarse (post-compromise
// security: comprometer una clave de un momento dado no compromete los
// turnos futuros una vez ambos lados han vuelto a intercambiar DH).
//
// Tolera mensajes fuera de orden o perdidos: las claves de mensajes que
// "faltan" se derivan igualmente y se guardan en `skippedKeys_` hasta que
// (si) llegan, con un limite maximo para evitar que un peer malicioso fuerce
// una asignacion de memoria descontrolada.
class DoubleRatchet {
 public:
  // Lado A (iniciador de X3DH). rootKey es la SK ya derivada por X3DH.
  // myRatchetKeyPair es el par DH propio desde el que arranca -- en la
  // practica, la misma efimera EK_A ya generada para X3DH, reaprovechada
  // como primer par de ratchet. theirRatchetPub es la signed prekey publica
  // de B (la misma usada en X3DH). associatedData debe construirse como
  // IK_iniciador || IK_receptor (en ese orden) para que ambos lados calculen
  // exactamente los mismos bytes.
  static DoubleRatchet initAsSender(const SharedSecret& rootKey,
                                    const X25519KeyPair& myRatchetKeyPair,
                                    const unsigned char theirRatchetPub[crypto_box_PUBLICKEYBYTES],
                                    const Bytes& associatedData);

  // Lado B (receptor de X3DH). rootKey es la misma SK derivada por X3DH.
  // myRatchetKeyPair es su propio par -- en la practica, su signed prekey,
  // reaprovechada como primer par de ratchet. Todavia no conoce la clave DH
  // de A: se establece automaticamente al procesar el primer mensaje
  // recibido (decrypt dispara el primer paso de DH ratchet).
  static DoubleRatchet initAsReceiver(const SharedSecret& rootKey,
                                      const X25519KeyPair& myRatchetKeyPair,
                                      const Bytes& associatedData);

  // Trabaja con bytes crudos (no solo texto): el payload de aplicacion
  // puede ser texto, o un fragmento binario de una transferencia de
  // archivo -- ver MessagePayload en el cliente para el pequeno framing
  // interno que distingue uno de otro dentro de este plaintext.
  RatchetMessage encrypt(const Bytes& plaintext);
  Bytes decrypt(const RatchetMessage& msg);

  // Vuelca TODO el estado (claves de cadena, claves saltadas pendientes,
  // contadores) a un blob binario para persistencia local. El blob contiene
  // material criptografico sensible en claro -- quien lo guarde en disco es
  // responsable de cifrarlo (ver LocalStore en el cliente).
  Bytes serialize() const;
  static DoubleRatchet deserialize(const Bytes& data);

 private:
  using ChainKey = std::array<unsigned char, 32>;
  using RootKey = std::array<unsigned char, 32>;
  using MessageKey = std::array<unsigned char, 32>;
  using DhPubBytes = std::array<unsigned char, crypto_box_PUBLICKEYBYTES>;

  DoubleRatchet() = default;

  void dhRatchetStep(const unsigned char theirNewDhPub[crypto_box_PUBLICKEYBYTES]);
  void skipMessageKeys(uint32_t until);
  std::optional<MessageKey> tryPopSkippedKey(const RatchetHeader& header);
  Bytes fullAd(const RatchetHeader& header) const;

  Bytes associatedData_;

  X25519KeyPair dhSelf_{};
  unsigned char dhRemote_[crypto_box_PUBLICKEYBYTES]{};
  bool hasDhRemote_ = false;

  RootKey rootKey_{};
  std::optional<ChainKey> chainKeySend_;
  std::optional<ChainKey> chainKeyRecv_;

  uint32_t Ns_ = 0;
  uint32_t Nr_ = 0;
  uint32_t PN_ = 0;

  // Cota de mensajes que se pueden saltar de una vez: protege contra un peer
  // (o un atacante activo) que declare un numero de mensaje absurdo para
  // forzar al receptor a derivar y guardar millones de claves.
  static constexpr uint32_t kMaxSkip = 1000;
  std::map<std::pair<DhPubBytes, uint32_t>, MessageKey> skippedKeys_;
};

}  // namespace templar::crypto
