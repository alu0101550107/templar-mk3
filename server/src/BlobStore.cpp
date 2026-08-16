#include "BlobStore.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace templar::server {

namespace fs = std::filesystem;

BlobStore::BlobStore(Database& db, std::string storageDir, uint64_t maxBlobBytes,
                     std::chrono::seconds retention)
    : db_(db),
      storageDir_(std::move(storageDir)),
      maxBlobBytes_(maxBlobBytes),
      retention_(retention) {
  fs::create_directories(storageDir_);
}

std::string BlobStore::filePathFor(const std::string& blobId) const {
  return storageDir_ + "/" + blobId;
}

std::string BlobStore::beginUpload(const std::string& ownerUsername, uint64_t totalSize) {
  if (totalSize > maxBlobBytes_) {
    throw std::runtime_error("El archivo supera el limite de tamano del servidor.");
  }

  std::string blobId = db_.createBlobRecord(ownerUsername, totalSize);

  std::ofstream out(filePathFor(blobId), std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("No se pudo crear el archivo de destino en el servidor.");
  }
  return blobId;
}

void BlobStore::appendChunk(const std::string& blobId, const std::string& ownerUsername,
                            const Bytes& chunk) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto rec = db_.findBlob(blobId);
  if (!rec || rec->ownerUsername != ownerUsername || rec->complete) {
    throw std::runtime_error("Blob invalido, ajeno, o ya completado.");
  }

  std::string path = filePathFor(blobId);
  std::error_code ec;
  uint64_t currentSize = fs::file_size(path, ec);
  if (ec) currentSize = 0;
  if (currentSize + chunk.size() > maxBlobBytes_) {
    throw std::runtime_error("El archivo supera el limite de tamano del servidor.");
  }

  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) {
    throw std::runtime_error("No se pudo escribir en el archivo de destino en el servidor.");
  }
  // Cada fragmento se guarda con un prefijo de longitud (mismo formato que
  // Writer::blob en Wire.hpp) en vez de concatenarlo a pelo -- crypto_secretstream
  // descifra por "mensajes" (un push() del cliente = un pull() aqui), y el
  // tamano de cada mensaje cifrado NO coincide con un multiplo redondo de
  // kFileChunkSize (lleva 17 bytes extra de overhead de autenticacion) ni es
  // constante (el ultimo fragmento suele ser mas corto). Sin este prefijo, una
  // relectura a trozos de tamano fijo en handleDownloadBlob desalinea los
  // limites de mensaje y crypto_secretstream_xchacha20poly1305_pull falla en
  // cuanto el archivo ocupa mas de un fragmento.
  templar::proto::Writer framed;
  framed.blob(chunk);
  const templar::proto::Bytes& out_bytes = framed.bytes();
  out.write(reinterpret_cast<const char*>(out_bytes.data()),
           static_cast<std::streamsize>(out_bytes.size()));
  if (!out) {
    throw std::runtime_error("Fallo escribiendo un fragmento del blob.");
  }
}

void BlobStore::finishUpload(const std::string& blobId, const std::string& ownerUsername) {
  auto rec = db_.findBlob(blobId);
  if (!rec || rec->ownerUsername != ownerUsername) {
    throw std::runtime_error("Blob invalido o ajeno.");
  }
  db_.markBlobComplete(blobId);
}

std::optional<std::string> BlobStore::pathForCompletedBlob(const std::string& blobId) {
  auto rec = db_.findBlob(blobId);
  if (!rec || !rec->complete) return std::nullopt;
  std::string path = filePathFor(blobId);
  if (!fs::exists(path)) return std::nullopt;
  return path;
}

size_t BlobStore::cleanupExpired() {
  int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - retention_.count();
  std::vector<std::string> expired = db_.listBlobsCreatedBefore(cutoff);
  for (const std::string& blobId : expired) {
    std::error_code ec;
    fs::remove(filePathFor(blobId), ec);
    db_.deleteBlobRecord(blobId);
  }
  return expired.size();
}

}  // namespace templar::server
