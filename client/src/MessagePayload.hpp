#pragma once

#include <cstdint>
#include <string>

#include "templar/Wire.hpp"

namespace templar::client {

using templar::proto::Bytes;

// Framing interno del "plaintext" que viaja dentro de cada mensaje del
// Double Ratchet: un byte de tipo + payload especifico. Permite distinguir
// texto normal de los distintos pasos de una transferencia de archivo sin
// tocar el protocolo de red -- el servidor sigue viendo, en cualquiera de
// los casos, un blob de ciphertext opaco (ver Router.cpp).
enum class PayloadKind : uint8_t {
  Text = 0,
  FileMeta = 1,
  FileChunk = 2,
  FileEnd = 3,
  // Puntero a un archivo subido como blob al servidor (ver el comentario
  // de UploadBlobBegin en Protocol.hpp) -- sustituye a
  // FileMeta/FileChunk/FileEnd para archivos nuevos; esos tres se
  // mantienen solo por si hiciera falta leer historial viejo, no se
  // vuelven a generar.
  FileBlobPointer = 4,
  // Mensaje de texto que responde a otro -- lleva ademas una COPIA
  // (recortada) del mensaje original, no una referencia/id: autocontenido
  // a proposito, sigue funcionando aunque el original ya no exista en el
  // historial local de quien lo recibe (o nunca haya llegado a tenerlo).
  // El servidor nunca ve nada de esto, viaja dentro del mismo ciphertext
  // que un mensaje de texto normal.
  TextReply = 5,
};

struct DecodedPayload {
  PayloadKind kind;
  std::string text;           // Text, TextReply (el texto nuevo)
  std::string replyToSender;  // TextReply -- nombre a mostrar de quien mando el original
  std::string replyToText;    // TextReply -- fragmento (ya recortado) del mensaje original
  std::string filename;       // FileMeta, FileBlobPointer
  uint64_t fileSize = 0;      // FileMeta, FileBlobPointer
  Bytes chunk;                // FileChunk
  std::string sha256Hex;      // FileEnd
  std::string blobId;         // FileBlobPointer
  Bytes fileKey;               // FileBlobPointer -- templar::crypto::FileKey en crudo
  Bytes fileHeader;            // FileBlobPointer -- templar::crypto::FileHeader en crudo
};

class MessagePayload {
 public:
  static Bytes encodeText(const std::string& text);
  static Bytes encodeTextReply(const std::string& text, const std::string& replyToSender,
                               const std::string& replyToText);
  static Bytes encodeFileBlobPointer(const std::string& blobId, const std::string& filename,
                                     uint64_t fileSize, const Bytes& fileKey,
                                     const Bytes& fileHeader);

  // Lanza std::runtime_error si el payload esta vacio, truncado, o el byte
  // de tipo no se reconoce (mensaje corrupto o forjado).
  static DecodedPayload decode(const Bytes& payload);
};

}  // namespace templar::client
