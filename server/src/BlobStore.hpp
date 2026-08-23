#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include "Database.hpp"
#include "templar/Wire.hpp"

namespace templar::server {

using templar::proto::Bytes;

// Almacena los blobs de archivo cifrados que suben los clientes (ver el
// comentario de UploadBlobBegin en Protocol.hpp). Los METADATOS (dueno,
// tamano, si la subida esta completa) viven en la tabla `blobs` de
// Database; los BYTES CIFRADOS EN SI viven en disco, un archivo por blob,
// para no engordar el .sqlite con datos potencialmente grandes ni tener
// que cargar un blob entero en memoria para leerlo o escribirlo. El
// servidor nunca ve el contenido en claro -- lo que guarda aqui es
// indistinguible de ruido para el sin la clave, que viaja solo dentro del
// mensaje FileBlobPointer cifrado a cada destinatario.
class BlobStore {
 public:
  BlobStore(Database& db, std::string storageDir, uint64_t maxBlobBytes,
           uint64_t maxBlobBytesPerUser, std::chrono::seconds retention);

  // Genera un id nuevo, reserva la fila en la BD (complete=0) y crea el
  // archivo vacio en disco. Lanza si totalSize declarado ya supera
  // maxBlobBytes (el limite real se sigue aplicando fragmento a fragmento
  // en appendChunk, por si un cliente miente sobre el tamano declarado), o
  // si sumado a lo que este usuario ya tiene ocupado (todos sus blobs,
  // completos o no) superaria maxBlobBytesPerUser.
  std::string beginUpload(const std::string& ownerUsername, uint64_t totalSize);

  // Anade un fragmento al final del archivo en disco. Lanza si el blobId
  // no existe, no es de ese usuario, ya se marco completo, o el total
  // acumulado superaria maxBlobBytes.
  void appendChunk(const std::string& blobId, const std::string& ownerUsername,
                   const Bytes& chunk);

  // Marca el blob como completo (ya se puede descargar). Lanza si el
  // blobId no existe o no es de ese usuario.
  void finishUpload(const std::string& blobId, const std::string& ownerUsername);

  // Ruta en disco de un blob YA completo, o nullopt si no existe, la
  // subida no termino, o el archivo desaparecio del disco. No comprueba
  // quien pide descargarlo mas alla de eso -- el propio blobId hace de
  // capability (ver el comentario de randomBlobId en Database.cpp).
  std::optional<std::string> pathForCompletedBlob(const std::string& blobId);

  // Borra de disco y de la BD los blobs subidos hace mas de `retention` --
  // pensado para llamarse periodicamente (ver el temporizador de limpieza
  // en main.cpp). Devuelve cuantos borro, solo para el log de arranque.
  size_t cleanupExpired();

 private:
  std::string filePathFor(const std::string& blobId) const;

  Database& db_;
  std::string storageDir_;
  uint64_t maxBlobBytes_;
  uint64_t maxBlobBytesPerUser_;
  std::chrono::seconds retention_;
  std::mutex mutex_;  // serializa las escrituras en disco de un mismo blob
};

}  // namespace templar::server
