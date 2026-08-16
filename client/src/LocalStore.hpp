#pragma once

#include <sqlite3.h>

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "templar/Wire.hpp"
#include "templar/crypto/Identity.hpp"

namespace templar::client {

using templar::proto::Bytes;

enum class UnlockResult {
  LoadedExisting,  // ya existia un almacen para ese usuario y la contrasena lo desbloqueo
  CreatedNew,      // no existia almacen para ese usuario -- se creo uno vacio
  WrongPassword,   // existia un almacen pero la contrasena no lo descifra
};

struct PersistedIdentity {
  templar::crypto::Ed25519KeyPair ed;
  templar::crypto::X25519KeyPair x;
  templar::crypto::X25519KeyPair signedPrekey;
  Bytes signedPrekeySig;
};

// Una linea de historial guardada tal cual (sin colores ni HTML) para que
// se pueda re-renderizar con el tema visual que este activo en el momento
// de mostrarla, incluso si el usuario cambio los colores despues de que se
// guardara. `kind` es un entero opaco para LocalStore -- quien lo llama
// (MainWindow) define su propio enum y castea.
struct HistoryLine {
  int kind;
  std::string who;
  std::string text;
  // Epoch en segundos (UTC). 0 = desconocido -- mensajes guardados antes de
  // que el historial tuviera hora (migracion no destructiva, ver
  // migrateSchema); MainWindow los renderiza sin el prefijo [HH:MM].
  int64_t createdAt;
  // true si `text` es HTML crudo (p.ej. el enlace "Descargar" de un
  // FileBlobPointer) en vez de texto plano a escapar -- ver
  // ChatLine::rawHtml en MainWindow.hpp/ClientController.hpp. Persistido a
  // proposito desde que los enlaces de descarga sobreviven a un reinicio
  // (ver PendingBlobDownloadRecord).
  bool rawHtml = false;
};

// Un puntero a archivo recibido y todavia sin descargar, persistido para
// que el enlace "Descargar" siga funcionando tras cerrar y reabrir la app
// (antes solo vivia en memoria -- ver el historial de PendingBlobDownload
// en MainWindow.hpp/ClientController.hpp). Se borra de aqui en cuanto la
// descarga termina (con exito o sin el, ver onBlobEndReceived/
// onBlobNotFound) -- si nunca se descarga, se queda hasta que el blob
// caduque en el servidor (30 dias) y una futura descarga falle con
// BlobNotFound, momento en el que tambien se borra.
struct PendingBlobDownloadRecord {
  std::string blobId;
  std::string originKey;
  std::string sender;
  std::string filename;
  uint64_t fileSize;
  Bytes fileKey;
  Bytes fileHeader;
};

// Almacen local cifrado: identidad, sesiones de Double Ratchet establecidas
// e historial de conversaciones -- todo lo que antes solo vivia en RAM y se
// perdia al cerrar el cliente.
//
// Se guarda como un unico blob en disco: una base de datos SQLite completa,
// mantenida en memoria durante la sesion (sqlite3_serialize/deserialize) y
// cifrada por encima con XChaCha20-Poly1305, con la clave derivada via
// Argon2id de la contrasena de la cuenta (se reusa esa contrasena a
// proposito -- ver discusion de diseno -- en vez de pedir una passphrase
// local aparte). Se vuelve a cifrar y escribir a disco tras cada mutacion.
//
// LIMITACION CONOCIDA: no hay sincronizacion entre dispositivos. Si este
// archivo se pierde, o la cuenta se usa por primera vez en otra maquina,
// ese dispositivo arranca con una identidad nueva (ver CreatedNew) y las
// conversaciones con peers que ya tenian la identidad vieja dejan de
// descifrar hasta reiniciar el intercambio.
class LocalStore {
 public:
  LocalStore() = default;
  ~LocalStore();

  LocalStore(const LocalStore&) = delete;
  LocalStore& operator=(const LocalStore&) = delete;

  UnlockResult unlock(const std::string& username, const std::string& password);

  // Borra cualquier almacen local previo para este usuario y crea uno
  // nuevo vacio. Se usa tras un REGISTRO exitoso: una cuenta recien
  // registrada nunca debe heredar la identidad/sesiones de un almacen
  // local anterior con el mismo nombre de usuario (p.ej. de pruebas contra
  // otro servidor, o una cuenta vieja borrada y re-registrada) -- eso es
  // justo lo que causaba que la identidad del cliente dejara de coincidir
  // con la que el servidor tiene guardada.
  UnlockResult resetAndUnlock(const std::string& username, const std::string& password);

  // Cierra la base de datos y borra la clave de memoria. Deja el objeto
  // listo para un unlock() posterior (p.ej. tras Desconectar).
  void lock();

  bool isUnlocked() const { return db_ != nullptr; }

  // --- Identidad ---
  void saveIdentity(const PersistedIdentity& identity);
  std::optional<PersistedIdentity> loadIdentity();

  // --- Sesiones de Double Ratchet (blob opaco: ver DoubleRatchet::serialize) ---
  void saveSession(const std::string& peer, const Bytes& ratchetState);
  std::optional<Bytes> loadSession(const std::string& peer);
  std::vector<std::string> listSessionPeers();

  // --- Historial de conversaciones ---
  // conversationKey: username del peer, o "" para el log de sistema.
  void appendHistoryLine(const std::string& conversationKey, int kind, const std::string& who,
                         const std::string& text, bool rawHtml = false);
  std::vector<HistoryLine> loadHistory(const std::string& conversationKey);
  std::vector<std::string> listConversationKeys();

  // --- Punteros a archivo recibidos y pendientes de descargar ---
  void savePendingBlobDownload(const PendingBlobDownloadRecord& rec);
  void deletePendingBlobDownload(const std::string& blobId);
  std::vector<PendingBlobDownloadRecord> loadPendingBlobDownloads();

  // --- Contadores de "no leido" por conversacion ---
  // Persistentes a proposito: si llega un mensaje mientras esa conversacion
  // no esta abierta y cierras la app sin haberla mirado, el contador sigue
  // ahi la proxima vez que inicies sesion (a diferencia de simplemente
  // guardarlo en memoria, que se perderia).
  void setUnreadCount(const std::string& conversationKey, int count);
  std::unordered_map<std::string, int> loadUnreadCounts();

  // --- Grupos conocidos ---
  // Solo el id: el nombre/admin/miembros los da el servidor de nuevo en
  // cada login (ver MsgType::MyGroups), pero el CLIENTE necesita poder
  // reconocer que una conversation_key con historial local es (o fue) un
  // grupo incluso DESPUES de perder la membresia (expulsado, salido, o el
  // grupo se borro) -- si no, al recargar el historial local antes de que
  // llegue el snapshot del servidor, esa clave se confundiria con un peer
  // normal (aparece con el id en crudo como nombre y un punto de
  // presencia falso). Ver MainWindow::loadHistoryFromStore.
  void rememberGroupKey(const std::string& groupId);
  std::vector<std::string> loadKnownGroupKeys();

  // --- One-time prekeys (X3DH) ---
  // Pareja publica/privada generada localmente; la publica se sube al
  // servidor (ver MsgType::PublishOtpk) para que la reparta UNA sola vez a
  // quien inicie una conversacion nueva. La privada se queda aqui hasta que
  // ESTE cliente vea esa misma clave publica en un primer mensaje entrante
  // -- entonces se usa para la 4a DH de X3DH y se borra (un solo uso: si
  // se reusara, se perderia la propiedad de seguridad que aporta).
  void saveOneTimePrekey(const Bytes& publicKey, const Bytes& secretKey);
  size_t countOneTimePrekeys();
  std::vector<Bytes> listOneTimePrekeyPublics();
  // Busca por clave publica, devuelve la privada y BORRA la fila si la
  // encuentra. std::nullopt si no hay ninguna con esa clave publica (no
  // deberia pasar en operacion normal -- indicaria un almacen local
  // desincronizado del pool que el servidor cree que tenemos).
  std::optional<Bytes> takeOneTimePrekeySecret(const Bytes& publicKey);

 private:
  void ensureSchema();
  void migrateSchema();
  void deriveKey(const std::string& password);
  void persistToDisk();
  std::string filePathFor(const std::string& username) const;

  sqlite3* db_ = nullptr;
  std::array<unsigned char, 32> key_{};
  std::array<unsigned char, crypto_pwhash_SALTBYTES> salt_{};
  std::string filePath_;
};

}  // namespace templar::client
