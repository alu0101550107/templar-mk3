#include "templar/crypto/DoubleRatchet.hpp"

#include <algorithm>
#include <stdexcept>

namespace templar::crypto {

namespace {

using ChainKey = std::array<unsigned char, 32>;
using RootKey = std::array<unsigned char, 32>;
using MessageKey = std::array<unsigned char, 32>;

// KDF_RK: mezcla la raiz actual con una nueva salida DH y produce la
// siguiente raiz + la primera clave de una cadena simetrica nueva.
std::pair<RootKey, ChainKey> kdfRootKey(const RootKey& rk, const DhOutput& dhOut) {
  unsigned char prk[crypto_kdf_hkdf_sha256_KEYBYTES];
  crypto_kdf_hkdf_sha256_extract(prk, rk.data(), rk.size(), dhOut.data(), dhOut.size());

  unsigned char out[64];
  static const char kInfo[] = "Templar_DR_RootKDF";
  crypto_kdf_hkdf_sha256_expand(out, sizeof(out), kInfo, sizeof(kInfo) - 1, prk);

  RootKey newRk;
  ChainKey newCk;
  std::copy(out, out + 32, newRk.begin());
  std::copy(out + 32, out + 64, newCk.begin());

  sodium_memzero(prk, sizeof(prk));
  sodium_memzero(out, sizeof(out));
  return {newRk, newCk};
}

// KDF_CK: avanza una cadena simetrica un paso, produciendo la siguiente
// chain key y una message key de un solo uso, via HMAC-SHA256 (como en la
// especificacion de Signal).
std::pair<ChainKey, MessageKey> kdfChainKey(const ChainKey& ck) {
  static const unsigned char kCkInput = 0x01;
  static const unsigned char kMkInput = 0x02;

  ChainKey newCk;
  MessageKey mk;
  crypto_auth_hmacsha256(newCk.data(), &kCkInput, 1, ck.data());
  crypto_auth_hmacsha256(mk.data(), &kMkInput, 1, ck.data());
  return {newCk, mk};
}

// Nonce fijo a cero: seguro porque cada `mk` se deriva una unica vez y se
// usa para cifrar exactamente un mensaje (la cadena simetrica nunca repite
// una message key), asi que el par (clave, nonce) nunca se reutiliza.
Bytes aeadEncrypt(const MessageKey& mk, const Bytes& plaintext, const Bytes& ad) {
  unsigned char nonce[crypto_aead_xchacha20poly1305_IETF_NPUBBYTES] = {0};

  Bytes ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long ciphertextLen = 0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertextLen, plaintext.data(),
                                             plaintext.size(), ad.data(), ad.size(), nullptr, nonce,
                                             mk.data());
  ciphertext.resize(ciphertextLen);
  return ciphertext;
}

Bytes aeadDecrypt(const MessageKey& mk, const Bytes& ciphertext, const Bytes& ad) {
  unsigned char nonce[crypto_aead_xchacha20poly1305_IETF_NPUBBYTES] = {0};

  if (ciphertext.size() < crypto_aead_xchacha20poly1305_IETF_ABYTES) {
    throw std::runtime_error("DoubleRatchet: ciphertext demasiado corto (corrupto)");
  }

  Bytes plaintext(ciphertext.size() - crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long plaintextLen = 0;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &plaintextLen, nullptr,
                                                 ciphertext.data(), ciphertext.size(), ad.data(),
                                                 ad.size(), nonce, mk.data()) != 0) {
    throw std::runtime_error(
        "DoubleRatchet: fallo de autenticacion (mensaje forjado, corrupto o fuera de secuencia)");
  }
  plaintext.resize(plaintextLen);
  return plaintext;
}

// --- Serializacion binaria plana para persistencia local (ver LocalStore en
// el cliente). El blob resultante contiene material criptografico sensible
// en claro -- quien lo persista es responsable de cifrarlo en disco.

class StateWriter {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void u32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v >> 24));
    buf_.push_back(static_cast<uint8_t>(v >> 16));
    buf_.push_back(static_cast<uint8_t>(v >> 8));
    buf_.push_back(static_cast<uint8_t>(v));
  }
  void raw(const unsigned char* data, size_t len) { buf_.insert(buf_.end(), data, data + len); }
  void blob(const Bytes& b) {
    u32(static_cast<uint32_t>(b.size()));
    raw(b.data(), b.size());
  }
  Bytes take() { return std::move(buf_); }

 private:
  Bytes buf_;
};

class StateReader {
 public:
  explicit StateReader(const Bytes& b) : data_(b.data()), len_(b.size()) {}

  uint8_t u8() {
    need(1);
    return data_[pos_++];
  }
  uint32_t u32() {
    need(4);
    uint32_t v = (uint32_t(data_[pos_]) << 24) | (uint32_t(data_[pos_ + 1]) << 16) |
                 (uint32_t(data_[pos_ + 2]) << 8) | uint32_t(data_[pos_ + 3]);
    pos_ += 4;
    return v;
  }
  void raw(unsigned char* out, size_t len) {
    need(len);
    std::copy(data_ + pos_, data_ + pos_ + len, out);
    pos_ += len;
  }
  Bytes blob() {
    uint32_t n = u32();
    need(n);
    Bytes b(data_ + pos_, data_ + pos_ + n);
    pos_ += n;
    return b;
  }

 private:
  void need(size_t n) const {
    if (pos_ + n > len_) {
      throw std::runtime_error("DoubleRatchet::deserialize: datos truncados o corruptos");
    }
  }
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
};

}  // namespace

Bytes RatchetHeader::serialize() const {
  Bytes out;
  out.reserve(crypto_box_PUBLICKEYBYTES + 8);
  out.insert(out.end(), dhPub, dhPub + crypto_box_PUBLICKEYBYTES);

  auto putU32 = [&out](uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
  };
  putU32(prevChainLen);
  putU32(messageNumber);
  return out;
}

RatchetHeader RatchetHeader::parse(const Bytes& data) {
  if (data.size() != static_cast<size_t>(crypto_box_PUBLICKEYBYTES) + 8) {
    throw std::runtime_error("RatchetHeader::parse: tamano invalido");
  }

  RatchetHeader h;
  std::copy(data.begin(), data.begin() + crypto_box_PUBLICKEYBYTES, h.dhPub);

  size_t off = crypto_box_PUBLICKEYBYTES;
  auto getU32 = [&data](size_t o) {
    return (uint32_t(data[o]) << 24) | (uint32_t(data[o + 1]) << 16) |
           (uint32_t(data[o + 2]) << 8) | uint32_t(data[o + 3]);
  };
  h.prevChainLen = getU32(off);
  h.messageNumber = getU32(off + 4);
  return h;
}

Bytes DoubleRatchet::fullAd(const RatchetHeader& header) const {
  Bytes ad = associatedData_;
  Bytes headerBytes = header.serialize();
  ad.insert(ad.end(), headerBytes.begin(), headerBytes.end());
  return ad;
}

DoubleRatchet DoubleRatchet::initAsSender(const SharedSecret& rootKey,
                                          const X25519KeyPair& myRatchetKeyPair,
                                          const unsigned char theirRatchetPub[crypto_box_PUBLICKEYBYTES],
                                          const Bytes& associatedData) {
  DoubleRatchet dr;
  dr.associatedData_ = associatedData;
  dr.dhSelf_ = myRatchetKeyPair;
  std::copy(theirRatchetPub, theirRatchetPub + crypto_box_PUBLICKEYBYTES, dr.dhRemote_);
  dr.hasDhRemote_ = true;
  std::copy(rootKey.begin(), rootKey.end(), dr.rootKey_.begin());

  DhOutput dhOut = diffieHellman(dr.dhSelf_.sk, dr.dhRemote_);
  auto [newRk, newCk] = kdfRootKey(dr.rootKey_, dhOut);
  dr.rootKey_ = newRk;
  dr.chainKeySend_ = newCk;
  // chainKeyRecv_ queda vacio: se establece cuando B responda con su propio
  // paso de DH ratchet (ver dhRatchetStep en el primer decrypt()).
  return dr;
}

DoubleRatchet DoubleRatchet::initAsReceiver(const SharedSecret& rootKey,
                                            const X25519KeyPair& myRatchetKeyPair,
                                            const Bytes& associatedData) {
  DoubleRatchet dr;
  dr.associatedData_ = associatedData;
  dr.dhSelf_ = myRatchetKeyPair;
  dr.hasDhRemote_ = false;
  std::copy(rootKey.begin(), rootKey.end(), dr.rootKey_.begin());
  // chainKeySend_/chainKeyRecv_ vacios: ambos se rellenan en el primer
  // dhRatchetStep, disparado automaticamente al decodificar el primer
  // mensaje recibido de A.
  return dr;
}

void DoubleRatchet::dhRatchetStep(const unsigned char theirNewDhPub[crypto_box_PUBLICKEYBYTES]) {
  PN_ = Ns_;
  Ns_ = 0;
  Nr_ = 0;
  std::copy(theirNewDhPub, theirNewDhPub + crypto_box_PUBLICKEYBYTES, dhRemote_);
  hasDhRemote_ = true;

  {
    DhOutput dhOut = diffieHellman(dhSelf_.sk, dhRemote_);
    auto [newRk, newCk] = kdfRootKey(rootKey_, dhOut);
    rootKey_ = newRk;
    chainKeyRecv_ = newCk;
  }

  dhSelf_ = generateX25519KeyPair();

  {
    DhOutput dhOut = diffieHellman(dhSelf_.sk, dhRemote_);
    auto [newRk, newCk] = kdfRootKey(rootKey_, dhOut);
    rootKey_ = newRk;
    chainKeySend_ = newCk;
  }
}

void DoubleRatchet::skipMessageKeys(uint32_t until) {
  if (!chainKeyRecv_) return;  // no hay cadena de recepcion activa todavia

  if (until > Nr_ + kMaxSkip) {
    throw std::runtime_error(
        "DoubleRatchet: se pidio saltar demasiados mensajes de golpe (posible DoS)");
  }

  DhPubBytes dhRemoteArr;
  std::copy(dhRemote_, dhRemote_ + crypto_box_PUBLICKEYBYTES, dhRemoteArr.begin());

  while (Nr_ < until) {
    auto [newCk, mk] = kdfChainKey(*chainKeyRecv_);
    chainKeyRecv_ = newCk;
    skippedKeys_[{dhRemoteArr, Nr_}] = mk;
    Nr_ += 1;
  }
}

std::optional<std::array<unsigned char, 32>> DoubleRatchet::tryPopSkippedKey(
    const RatchetHeader& header) {
  DhPubBytes dhArr;
  std::copy(header.dhPub, header.dhPub + crypto_box_PUBLICKEYBYTES, dhArr.begin());

  auto it = skippedKeys_.find({dhArr, header.messageNumber});
  if (it == skippedKeys_.end()) return std::nullopt;

  MessageKey mk = it->second;
  skippedKeys_.erase(it);
  return mk;
}

RatchetMessage DoubleRatchet::encrypt(const Bytes& plaintext) {
  if (!chainKeySend_) {
    throw std::runtime_error("DoubleRatchet: todavia no hay una cadena de envio establecida");
  }

  auto [newCk, mk] = kdfChainKey(*chainKeySend_);
  chainKeySend_ = newCk;

  RatchetMessage out;
  std::copy(dhSelf_.pk, dhSelf_.pk + crypto_box_PUBLICKEYBYTES, out.header.dhPub);
  out.header.prevChainLen = PN_;
  out.header.messageNumber = Ns_;
  Ns_ += 1;

  out.ciphertext = aeadEncrypt(mk, plaintext, fullAd(out.header));
  sodium_memzero(mk.data(), mk.size());
  return out;
}

Bytes DoubleRatchet::decrypt(const RatchetMessage& msg) {
  if (auto skipped = tryPopSkippedKey(msg.header)) {
    Bytes pt = aeadDecrypt(*skipped, msg.ciphertext, fullAd(msg.header));
    sodium_memzero(skipped->data(), skipped->size());
    return pt;
  }

  bool isNewRatchetKey =
      !hasDhRemote_ ||
      !std::equal(msg.header.dhPub, msg.header.dhPub + crypto_box_PUBLICKEYBYTES, dhRemote_);

  if (isNewRatchetKey) {
    skipMessageKeys(msg.header.prevChainLen);  // agota lo que quedaba de la cadena vieja
    dhRatchetStep(msg.header.dhPub);
  }

  skipMessageKeys(msg.header.messageNumber);

  auto [newCk, mk] = kdfChainKey(*chainKeyRecv_);
  chainKeyRecv_ = newCk;
  Nr_ += 1;

  Bytes pt = aeadDecrypt(mk, msg.ciphertext, fullAd(msg.header));
  sodium_memzero(mk.data(), mk.size());
  return pt;
}

Bytes DoubleRatchet::serialize() const {
  StateWriter w;
  w.blob(associatedData_);
  w.raw(dhSelf_.pk, crypto_box_PUBLICKEYBYTES);
  w.raw(dhSelf_.sk, crypto_box_SECRETKEYBYTES);
  w.u8(hasDhRemote_ ? 1 : 0);
  w.raw(dhRemote_, crypto_box_PUBLICKEYBYTES);
  w.raw(rootKey_.data(), rootKey_.size());

  static const ChainKey kZeroChainKey{};
  w.u8(chainKeySend_ ? 1 : 0);
  w.raw((chainKeySend_ ? *chainKeySend_ : kZeroChainKey).data(), 32);
  w.u8(chainKeyRecv_ ? 1 : 0);
  w.raw((chainKeyRecv_ ? *chainKeyRecv_ : kZeroChainKey).data(), 32);

  w.u32(Ns_);
  w.u32(Nr_);
  w.u32(PN_);

  w.u32(static_cast<uint32_t>(skippedKeys_.size()));
  for (const auto& entry : skippedKeys_) {
    const DhPubBytes& dhPub = entry.first.first;
    uint32_t msgNum = entry.first.second;
    const MessageKey& mk = entry.second;
    w.raw(dhPub.data(), dhPub.size());
    w.u32(msgNum);
    w.raw(mk.data(), mk.size());
  }

  return w.take();
}

DoubleRatchet DoubleRatchet::deserialize(const Bytes& data) {
  StateReader r(data);
  DoubleRatchet dr;

  dr.associatedData_ = r.blob();
  r.raw(dr.dhSelf_.pk, crypto_box_PUBLICKEYBYTES);
  r.raw(dr.dhSelf_.sk, crypto_box_SECRETKEYBYTES);
  dr.hasDhRemote_ = r.u8() != 0;
  r.raw(dr.dhRemote_, crypto_box_PUBLICKEYBYTES);
  r.raw(dr.rootKey_.data(), dr.rootKey_.size());

  bool hasSend = r.u8() != 0;
  ChainKey sendCk;
  r.raw(sendCk.data(), sendCk.size());
  if (hasSend) dr.chainKeySend_ = sendCk;

  bool hasRecv = r.u8() != 0;
  ChainKey recvCk;
  r.raw(recvCk.data(), recvCk.size());
  if (hasRecv) dr.chainKeyRecv_ = recvCk;

  dr.Ns_ = r.u32();
  dr.Nr_ = r.u32();
  dr.PN_ = r.u32();

  uint32_t skippedCount = r.u32();
  for (uint32_t i = 0; i < skippedCount; ++i) {
    DhPubBytes dhPub;
    r.raw(dhPub.data(), dhPub.size());
    uint32_t msgNum = r.u32();
    MessageKey mk;
    r.raw(mk.data(), mk.size());
    dr.skippedKeys_[{dhPub, msgNum}] = mk;
  }

  return dr;
}

}  // namespace templar::crypto
