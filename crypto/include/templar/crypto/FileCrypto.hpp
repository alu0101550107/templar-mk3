#pragma once

#include <sodium.h>

#include <array>
#include <cstdint>
#include <vector>

namespace templar::crypto {

using Bytes = std::vector<uint8_t>;

// Cifrado de archivos por streaming con clave propia, ajena a cualquier
// sesion Double Ratchet -- una clave aleatoria de un solo uso por archivo
// (ver el comentario de UploadBlobBegin en Protocol.hpp para el porque: un
// archivo se cifra UNA vez, no una vez por destinatario). Envoltorio fino
// sobre crypto_secretstream_xchacha20poly1305 de libsodium: a diferencia de
// partir el archivo en trozos y cifrar cada uno suelto con AEAD normal
// (lo que se hacia antes, un paso de Double Ratchet por trozo), esto
// autentica cada fragmento COMO PARTE de una secuencia y detecta que se
// haya recortado o reordenado el final -- sin eso, alguien podria truncar
// el archivo en transito sin que el receptor lo note.

constexpr size_t kFileKeyBytes = crypto_secretstream_xchacha20poly1305_KEYBYTES;
constexpr size_t kFileHeaderBytes = crypto_secretstream_xchacha20poly1305_HEADERBYTES;
// Overhead (tag de autenticacion) que anade cada fragmento cifrado -- el
// ciphertext de un trozo mide plaintext.size() + esto.
constexpr size_t kFileChunkOverheadBytes = crypto_secretstream_xchacha20poly1305_ABYTES;

using FileKey = std::array<unsigned char, kFileKeyBytes>;
using FileHeader = std::array<unsigned char, kFileHeaderBytes>;

FileKey generateFileKey();

// Un cifrador por archivo: se crea una vez, se le van pasando fragmentos en
// orden via encryptChunk(), marcando isLast=true solo en el ultimo.
class FileEncryptor {
 public:
  // Genera la cabecera (header()) a partir de key -- viaja junto a la
  // clave dentro de FileBlobPointer (ver MessagePayload.hpp), el receptor
  // la necesita para poder empezar a descifrar.
  explicit FileEncryptor(const FileKey& key);

  const FileHeader& header() const { return header_; }

  Bytes encryptChunk(const Bytes& plaintext, bool isLast);

 private:
  crypto_secretstream_xchacha20poly1305_state state_{};
  FileHeader header_{};
};

// Descifrador por archivo: un objeto por descarga, se le van pasando los
// fragmentos en el mismo orden en que se cifraron.
class FileDecryptor {
 public:
  FileDecryptor(const FileKey& key, const FileHeader& header);

  struct Result {
    Bytes plaintext;
    // true si este fragmento venia marcado como el ultimo -- quien llama
    // debe parar aqui, no seguir pidiendo mas fragmentos.
    bool isLast;
  };

  // Lanza std::runtime_error si falla la autenticacion del fragmento
  // (corrupto o manipulado) o si llega algo despues de uno ya marcado
  // como final.
  Result decryptChunk(const Bytes& ciphertext);

 private:
  crypto_secretstream_xchacha20poly1305_state state_{};
};

}  // namespace templar::crypto
