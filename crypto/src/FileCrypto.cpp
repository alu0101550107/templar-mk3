#include "templar/crypto/FileCrypto.hpp"

#include <stdexcept>
#include <utility>

namespace templar::crypto {

FileKey generateFileKey() {
  FileKey key{};
  crypto_secretstream_xchacha20poly1305_keygen(key.data());
  return key;
}

FileEncryptor::FileEncryptor(const FileKey& key) {
  if (crypto_secretstream_xchacha20poly1305_init_push(&state_, header_.data(), key.data()) != 0) {
    throw std::runtime_error("No se pudo inicializar el cifrado del archivo.");
  }
}

Bytes FileEncryptor::encryptChunk(const Bytes& plaintext, bool isLast) {
  Bytes ciphertext(plaintext.size() + kFileChunkOverheadBytes);
  unsigned long long ciphertextLen = 0;
  unsigned char tag = isLast ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                             : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
  if (crypto_secretstream_xchacha20poly1305_push(&state_, ciphertext.data(), &ciphertextLen,
                                                 plaintext.data(), plaintext.size(), nullptr, 0,
                                                 tag) != 0) {
    throw std::runtime_error("Fallo cifrando un fragmento del archivo.");
  }
  ciphertext.resize(static_cast<size_t>(ciphertextLen));
  return ciphertext;
}

FileDecryptor::FileDecryptor(const FileKey& key, const FileHeader& header) {
  if (crypto_secretstream_xchacha20poly1305_init_pull(&state_, header.data(), key.data()) != 0) {
    throw std::runtime_error("Cabecera de archivo invalida.");
  }
}

FileDecryptor::Result FileDecryptor::decryptChunk(const Bytes& ciphertext) {
  if (ciphertext.size() < kFileChunkOverheadBytes) {
    throw std::runtime_error("Fragmento de archivo demasiado corto.");
  }
  Bytes plaintext(ciphertext.size() - kFileChunkOverheadBytes);
  unsigned long long plaintextLen = 0;
  unsigned char tag = 0;
  if (crypto_secretstream_xchacha20poly1305_pull(&state_, plaintext.data(), &plaintextLen, &tag,
                                                 ciphertext.data(), ciphertext.size(), nullptr,
                                                 0) != 0) {
    throw std::runtime_error(
        "No se pudo descifrar un fragmento del archivo (corrupto o manipulado).");
  }
  plaintext.resize(static_cast<size_t>(plaintextLen));
  return Result{std::move(plaintext), tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL};
}

}  // namespace templar::crypto
