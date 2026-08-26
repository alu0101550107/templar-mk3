#include "LocalStore.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>

#include <cstring>
#include <ctime>
#include <stdexcept>

namespace templar::client {

namespace {

// RAII para sqlite3_stmt*, igual patron que server/src/Database.cpp.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("LocalStore: prepare fallo: ") + sqlite3_errmsg(db));
    }
  }
  ~Stmt() { sqlite3_finalize(stmt_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  operator sqlite3_stmt*() const { return stmt_; }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

void bindText(sqlite3_stmt* s, int idx, const std::string& v) {
  sqlite3_bind_text(s, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}

void bindBlob(sqlite3_stmt* s, int idx, const void* data, size_t len) {
  if (len == 0) {
    sqlite3_bind_zeroblob(s, idx, 0);
  } else {
    sqlite3_bind_blob(s, idx, data, static_cast<int>(len), SQLITE_TRANSIENT);
  }
}

Bytes columnBlob(sqlite3_stmt* s, int idx) {
  const void* data = sqlite3_column_blob(s, idx);
  int len = sqlite3_column_bytes(s, idx);
  if (data == nullptr || len == 0) return {};
  const auto* p = static_cast<const uint8_t*>(data);
  return Bytes(p, p + len);
}

void columnRaw(sqlite3_stmt* s, int idx, unsigned char* out, size_t expectedLen) {
  Bytes b = columnBlob(s, idx);
  if (b.size() != expectedLen) {
    throw std::runtime_error("LocalStore: columna con tamano inesperado (almacen corrupto)");
  }
  std::copy(b.begin(), b.end(), out);
}

std::string columnText(sqlite3_stmt* s, int idx) {
  const unsigned char* txt = sqlite3_column_text(s, idx);
  int len = sqlite3_column_bytes(s, idx);
  if (txt == nullptr) return {};
  return std::string(reinterpret_cast<const char*>(txt), len);
}

constexpr char kMagic[4] = {'T', 'D', 'R', '1'};

}  // namespace

LocalStore::~LocalStore() { lock(); }

void LocalStore::lock() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  sodium_memzero(key_.data(), key_.size());
}

std::string LocalStore::filePathFor(const std::string& username) const {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);

  QString safe;
  for (char c : username) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.';
    safe += ok ? QChar(c) : QChar('_');
  }
  if (safe.isEmpty()) safe = "usuario";

  return (dir + "/" + safe + ".tdb").toStdString();
}

void LocalStore::deriveKey(const std::string& password) {
  if (crypto_pwhash(key_.data(), key_.size(), password.c_str(), password.size(), salt_.data(),
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    throw std::runtime_error("LocalStore: fallo al derivar la clave local (sin memoria?)");
  }
}

void LocalStore::ensureSchema() {
  const char* sql =
      "CREATE TABLE IF NOT EXISTS identity ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  ed25519_pk BLOB NOT NULL,"
      "  ed25519_sk BLOB NOT NULL,"
      "  x25519_pk BLOB NOT NULL,"
      "  x25519_sk BLOB NOT NULL,"
      "  signed_prekey_pk BLOB NOT NULL,"
      "  signed_prekey_sk BLOB NOT NULL,"
      "  signed_prekey_sig BLOB NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS sessions ("
      "  peer_username TEXT PRIMARY KEY,"
      "  ratchet_state BLOB NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS history ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  conversation_key TEXT NOT NULL,"
      "  kind INTEGER NOT NULL,"
      "  who TEXT NOT NULL,"
      "  text TEXT NOT NULL,"
      "  created_at INTEGER NOT NULL DEFAULT 0,"
      "  raw_html INTEGER NOT NULL DEFAULT 0"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_history_conv ON history(conversation_key, id);"
      "CREATE TABLE IF NOT EXISTS unread_counts ("
      "  conversation_key TEXT PRIMARY KEY,"
      "  count INTEGER NOT NULL DEFAULT 0"
      ");"
      "CREATE TABLE IF NOT EXISTS known_groups ("
      "  group_id TEXT PRIMARY KEY"
      ");"
      "CREATE TABLE IF NOT EXISTS one_time_prekeys ("
      "  public_key BLOB PRIMARY KEY,"
      "  secret_key BLOB NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS pending_blob_downloads ("
      "  blob_id TEXT PRIMARY KEY,"
      "  origin_key TEXT NOT NULL,"
      "  sender TEXT NOT NULL,"
      "  filename TEXT NOT NULL,"
      "  file_size INTEGER NOT NULL,"
      "  file_key BLOB NOT NULL,"
      "  file_header BLOB NOT NULL"
      ");";

  char* errMsg = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "desconocido";
    sqlite3_free(errMsg);
    throw std::runtime_error("LocalStore: fallo creando el esquema: " + err);
  }
}

void LocalStore::migrateSchema() {
  // Compatibilidad con almacenes creados antes de que el historial pasara a
  // guardarse estructurado (kind/who/text) en vez de HTML ya coloreado
  // (Fase 4). No hay forma limpia de recuperar esos datos -- el HTML ya
  // tenia los colores horneados y no se puede separar de vuelta en
  // (kind, who, texto) -- asi que si detectamos el esquema viejo, se
  // recrea vacia en vez de dejar el almacen inutilizable.
  bool historyExists = false;
  bool hasKindColumn = false;
  bool hasCreatedAtColumn = false;
  bool hasRawHtmlColumn = false;
  {
    Stmt s(db_, "PRAGMA table_info(history);");
    while (sqlite3_step(s) == SQLITE_ROW) {
      historyExists = true;
      std::string colName = columnText(s, 1);  // columna 1 = nombre
      if (colName == "kind") hasKindColumn = true;
      if (colName == "created_at") hasCreatedAtColumn = true;
      if (colName == "raw_html") hasRawHtmlColumn = true;
    }
  }

  if (historyExists && !hasKindColumn) {
    // Esquema anterior a los datos estructurados (Fase 4): HTML ya
    // coloreado, sin forma limpia de separarlo en (kind, who, texto) -- se
    // recrea vacia en vez de dejar el almacen inutilizable.
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "DROP TABLE history;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::string err = errMsg ? errMsg : "desconocido";
      sqlite3_free(errMsg);
      throw std::runtime_error("LocalStore: fallo migrando el esquema de historial: " + err);
    }
  } else {
    if (historyExists && !hasCreatedAtColumn) {
      // Historial ya estructurado, solo le falta la hora: anadir la columna
      // preserva todos los mensajes existentes (quedan con created_at = 0,
      // que se renderiza sin el prefijo [HH:MM] en vez de inventarse una hora).
      char* errMsg = nullptr;
      if (sqlite3_exec(db_, "ALTER TABLE history ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0;",
                       nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "desconocido";
        sqlite3_free(errMsg);
        throw std::runtime_error("LocalStore: fallo anadiendo la columna created_at: " + err);
      }
    }
    if (historyExists && !hasRawHtmlColumn) {
      // Igual que created_at: anadir la columna preserva los mensajes
      // existentes (quedan con raw_html = 0, es decir texto plano -- los
      // pocos enlaces de descarga guardados antes de esta migracion ya se
      // persistian como texto plano de todos modos, ver el comentario de
      // PendingBlobDownloadRecord).
      char* errMsg = nullptr;
      if (sqlite3_exec(db_, "ALTER TABLE history ADD COLUMN raw_html INTEGER NOT NULL DEFAULT 0;",
                       nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "desconocido";
        sqlite3_free(errMsg);
        throw std::runtime_error("LocalStore: fallo anadiendo la columna raw_html: " + err);
      }
    }
  }

  ensureSchema();
}

UnlockResult LocalStore::unlock(const std::string& username, const std::string& password) {
  lock();

  filePath_ = filePathFor(username);
  QFile file(QString::fromStdString(filePath_));

  if (!file.exists()) {
    randombytes_buf(salt_.data(), salt_.size());
    deriveKey(password);

    if (sqlite3_open(":memory:", &db_) != SQLITE_OK) {
      throw std::runtime_error("LocalStore: no se pudo crear la base de datos en memoria");
    }
    migrateSchema();
    persistToDisk();
    return UnlockResult::CreatedNew;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error("LocalStore: no se pudo abrir el almacen local existente");
  }
  QByteArray raw = file.readAll();
  file.close();

  constexpr int kMagicLen = 4;
  constexpr int kSaltLen = crypto_pwhash_SALTBYTES;
  constexpr int kNonceLen = crypto_aead_xchacha20poly1305_IETF_NPUBBYTES;
  constexpr int kHeaderLen = kMagicLen + kSaltLen + kNonceLen;

  if (raw.size() < kHeaderLen || std::memcmp(raw.constData(), kMagic, kMagicLen) != 0) {
    throw std::runtime_error("LocalStore: archivo de almacen local invalido o corrupto");
  }

  std::copy(raw.begin() + kMagicLen, raw.begin() + kMagicLen + kSaltLen, salt_.begin());
  unsigned char nonce[kNonceLen];
  std::copy(raw.begin() + kMagicLen + kSaltLen, raw.begin() + kHeaderLen, nonce);

  deriveKey(password);

  Bytes ciphertext(raw.begin() + kHeaderLen, raw.end());
  if (ciphertext.size() < crypto_aead_xchacha20poly1305_IETF_ABYTES) {
    throw std::runtime_error("LocalStore: almacen local corrupto (demasiado corto)");
  }

  Bytes plaintext(ciphertext.size() - crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long plaintextLen = 0;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &plaintextLen, nullptr,
                                                 ciphertext.data(), ciphertext.size(), nullptr, 0,
                                                 nonce, key_.data()) != 0) {
    sodium_memzero(key_.data(), key_.size());
    return UnlockResult::WrongPassword;
  }
  plaintext.resize(plaintextLen);

  if (sqlite3_open(":memory:", &db_) != SQLITE_OK) {
    sodium_memzero(plaintext.data(), plaintext.size());
    throw std::runtime_error("LocalStore: no se pudo crear la base de datos en memoria");
  }

  // sqlite3_deserialize necesita un buffer reservado con sqlite3_malloc si
  // le pedimos que se encargue de liberarlo (SQLITE_DESERIALIZE_FREEONCLOSE).
  void* sqliteBuf = sqlite3_malloc64(plaintext.size());
  if (!sqliteBuf) {
    sodium_memzero(plaintext.data(), plaintext.size());
    throw std::runtime_error("LocalStore: sin memoria al cargar el almacen local");
  }
  std::memcpy(sqliteBuf, plaintext.data(), plaintext.size());
  size_t plaintextSize = plaintext.size();
  sodium_memzero(plaintext.data(), plaintext.size());

  int rc = sqlite3_deserialize(db_, "main", static_cast<unsigned char*>(sqliteBuf), plaintextSize,
                               plaintextSize,
                               SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("LocalStore: fallo al cargar la base de datos deserializada");
  }

  // Un almacen ya existente puede venir de una version anterior con un
  // esquema distinto -- migrar antes de que cualquier llamada posterior
  // (loadHistory, etc.) intente usar columnas que todavia no existen.
  migrateSchema();

  return UnlockResult::LoadedExisting;
}

UnlockResult LocalStore::resetAndUnlock(const std::string& username, const std::string& password) {
  lock();
  QFile::remove(QString::fromStdString(filePathFor(username)));
  return unlock(username, password);
}

void LocalStore::persistToDisk() {
  if (!db_) return;

  sqlite3_int64 size = 0;
  unsigned char* serialized = sqlite3_serialize(db_, "main", &size, 0);
  if (!serialized) {
    throw std::runtime_error("LocalStore: fallo al serializar la base de datos en memoria");
  }
  Bytes plaintext(serialized, serialized + size);
  sqlite3_free(serialized);

  unsigned char nonce[crypto_aead_xchacha20poly1305_IETF_NPUBBYTES];
  randombytes_buf(nonce, sizeof(nonce));

  Bytes ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long ciphertextLen = 0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertextLen, plaintext.data(),
                                             plaintext.size(), nullptr, 0, nullptr, nonce,
                                             key_.data());
  ciphertext.resize(ciphertextLen);
  sodium_memzero(plaintext.data(), plaintext.size());

  // Escritura atomica (temporal + rename) para no dejar el almacen a medio
  // escribir si el proceso muere en mitad de la operacion.
  QString finalPath = QString::fromStdString(filePath_);
  QString tmpPath = finalPath + ".tmp";

  QFile tmp(tmpPath);
  if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    throw std::runtime_error("LocalStore: no se pudo escribir el archivo temporal");
  }
  tmp.write(kMagic, sizeof(kMagic));
  tmp.write(reinterpret_cast<const char*>(salt_.data()), salt_.size());
  tmp.write(reinterpret_cast<const char*>(nonce), sizeof(nonce));
  tmp.write(reinterpret_cast<const char*>(ciphertext.data()),
           static_cast<qint64>(ciphertext.size()));
  tmp.close();

  QFile::remove(finalPath);
  if (!QFile::rename(tmpPath, finalPath)) {
    throw std::runtime_error("LocalStore: no se pudo reemplazar el almacen local");
  }
}

void LocalStore::saveIdentity(const PersistedIdentity& identity) {
  Stmt s(db_,
        "INSERT INTO identity (id, ed25519_pk, ed25519_sk, x25519_pk, x25519_sk, "
        "signed_prekey_pk, signed_prekey_sk, signed_prekey_sig) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  ed25519_pk=excluded.ed25519_pk, ed25519_sk=excluded.ed25519_sk, "
        "  x25519_pk=excluded.x25519_pk, x25519_sk=excluded.x25519_sk, "
        "  signed_prekey_pk=excluded.signed_prekey_pk, "
        "  signed_prekey_sk=excluded.signed_prekey_sk, "
        "  signed_prekey_sig=excluded.signed_prekey_sig;");
  bindBlob(s, 1, identity.ed.pk, crypto_sign_PUBLICKEYBYTES);
  bindBlob(s, 2, identity.ed.sk, crypto_sign_SECRETKEYBYTES);
  bindBlob(s, 3, identity.x.pk, crypto_box_PUBLICKEYBYTES);
  bindBlob(s, 4, identity.x.sk, crypto_box_SECRETKEYBYTES);
  bindBlob(s, 5, identity.signedPrekey.pk, crypto_box_PUBLICKEYBYTES);
  bindBlob(s, 6, identity.signedPrekey.sk, crypto_box_SECRETKEYBYTES);
  bindBlob(s, 7, identity.signedPrekeySig.data(), identity.signedPrekeySig.size());

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: saveIdentity fallo: ") + sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::optional<PersistedIdentity> LocalStore::loadIdentity() {
  Stmt s(db_,
        "SELECT ed25519_pk, ed25519_sk, x25519_pk, x25519_sk, signed_prekey_pk, "
        "signed_prekey_sk, signed_prekey_sig FROM identity WHERE id = 1;");
  if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;

  PersistedIdentity id;
  columnRaw(s, 0, id.ed.pk, crypto_sign_PUBLICKEYBYTES);
  columnRaw(s, 1, id.ed.sk, crypto_sign_SECRETKEYBYTES);
  columnRaw(s, 2, id.x.pk, crypto_box_PUBLICKEYBYTES);
  columnRaw(s, 3, id.x.sk, crypto_box_SECRETKEYBYTES);
  columnRaw(s, 4, id.signedPrekey.pk, crypto_box_PUBLICKEYBYTES);
  columnRaw(s, 5, id.signedPrekey.sk, crypto_box_SECRETKEYBYTES);
  id.signedPrekeySig = columnBlob(s, 6);
  return id;
}

void LocalStore::saveSession(const std::string& peer, const Bytes& ratchetState) {
  Stmt s(db_,
        "INSERT INTO sessions (peer_username, ratchet_state) VALUES (?, ?) "
        "ON CONFLICT(peer_username) DO UPDATE SET ratchet_state=excluded.ratchet_state;");
  bindText(s, 1, peer);
  bindBlob(s, 2, ratchetState.data(), ratchetState.size());

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: saveSession fallo: ") + sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::optional<Bytes> LocalStore::loadSession(const std::string& peer) {
  Stmt s(db_, "SELECT ratchet_state FROM sessions WHERE peer_username = ?;");
  bindText(s, 1, peer);
  if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
  return columnBlob(s, 0);
}

std::vector<std::string> LocalStore::listSessionPeers() {
  Stmt s(db_, "SELECT peer_username FROM sessions;");
  std::vector<std::string> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnText(s, 0));
  }
  return out;
}

void LocalStore::appendHistoryLine(const std::string& conversationKey, int kind,
                                   const std::string& who, const std::string& text,
                                   bool rawHtml, int64_t createdAt) {
  Stmt s(db_,
        "INSERT INTO history (conversation_key, kind, who, text, created_at, raw_html) "
        "VALUES (?, ?, ?, ?, ?, ?);");
  bindText(s, 1, conversationKey);
  sqlite3_bind_int(s, 2, kind);
  bindText(s, 3, who);
  bindText(s, 4, text);
  sqlite3_bind_int64(s, 5, createdAt != 0 ? createdAt : static_cast<int64_t>(std::time(nullptr)));
  sqlite3_bind_int(s, 6, rawHtml ? 1 : 0);

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: appendHistoryLine fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::vector<HistoryLine> LocalStore::loadHistory(const std::string& conversationKey) {
  Stmt s(db_,
        "SELECT kind, who, text, created_at, raw_html FROM history "
        "WHERE conversation_key = ? ORDER BY id ASC;");
  bindText(s, 1, conversationKey);

  std::vector<HistoryLine> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    HistoryLine line;
    line.kind = sqlite3_column_int(s, 0);
    line.who = columnText(s, 1);
    line.text = columnText(s, 2);
    line.createdAt = sqlite3_column_int64(s, 3);
    line.rawHtml = sqlite3_column_int(s, 4) != 0;
    out.push_back(std::move(line));
  }
  return out;
}

void LocalStore::savePendingBlobDownload(const PendingBlobDownloadRecord& rec) {
  Stmt s(db_,
        "INSERT INTO pending_blob_downloads "
        "(blob_id, origin_key, sender, filename, file_size, file_key, file_header) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(blob_id) DO UPDATE SET origin_key=excluded.origin_key, "
        "sender=excluded.sender, filename=excluded.filename, file_size=excluded.file_size, "
        "file_key=excluded.file_key, file_header=excluded.file_header;");
  bindText(s, 1, rec.blobId);
  bindText(s, 2, rec.originKey);
  bindText(s, 3, rec.sender);
  bindText(s, 4, rec.filename);
  sqlite3_bind_int64(s, 5, static_cast<int64_t>(rec.fileSize));
  bindBlob(s, 6, rec.fileKey.data(), rec.fileKey.size());
  bindBlob(s, 7, rec.fileHeader.data(), rec.fileHeader.size());

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: savePendingBlobDownload fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

void LocalStore::deletePendingBlobDownload(const std::string& blobId) {
  Stmt s(db_, "DELETE FROM pending_blob_downloads WHERE blob_id = ?;");
  bindText(s, 1, blobId);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: deletePendingBlobDownload fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::vector<PendingBlobDownloadRecord> LocalStore::loadPendingBlobDownloads() {
  Stmt s(db_,
        "SELECT blob_id, origin_key, sender, filename, file_size, file_key, file_header "
        "FROM pending_blob_downloads;");
  std::vector<PendingBlobDownloadRecord> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    PendingBlobDownloadRecord rec;
    rec.blobId = columnText(s, 0);
    rec.originKey = columnText(s, 1);
    rec.sender = columnText(s, 2);
    rec.filename = columnText(s, 3);
    rec.fileSize = static_cast<uint64_t>(sqlite3_column_int64(s, 4));
    rec.fileKey = columnBlob(s, 5);
    rec.fileHeader = columnBlob(s, 6);
    out.push_back(std::move(rec));
  }
  return out;
}

std::vector<std::string> LocalStore::listConversationKeys() {
  Stmt s(db_, "SELECT DISTINCT conversation_key FROM history;");
  std::vector<std::string> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnText(s, 0));
  }
  return out;
}

void LocalStore::setUnreadCount(const std::string& conversationKey, int count) {
  Stmt s(db_,
        "INSERT INTO unread_counts (conversation_key, count) VALUES (?, ?) "
        "ON CONFLICT(conversation_key) DO UPDATE SET count = excluded.count;");
  bindText(s, 1, conversationKey);
  sqlite3_bind_int(s, 2, count);

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: setUnreadCount fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::unordered_map<std::string, int> LocalStore::loadUnreadCounts() {
  Stmt s(db_, "SELECT conversation_key, count FROM unread_counts WHERE count > 0;");
  std::unordered_map<std::string, int> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out[columnText(s, 0)] = sqlite3_column_int(s, 1);
  }
  return out;
}

void LocalStore::rememberGroupKey(const std::string& groupId) {
  Stmt s(db_, "INSERT OR IGNORE INTO known_groups (group_id) VALUES (?);");
  bindText(s, 1, groupId);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: rememberGroupKey fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

std::vector<std::string> LocalStore::loadKnownGroupKeys() {
  Stmt s(db_, "SELECT group_id FROM known_groups;");
  std::vector<std::string> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnText(s, 0));
  }
  return out;
}

void LocalStore::saveOneTimePrekey(const Bytes& publicKey, const Bytes& secretKey) {
  Stmt s(db_, "INSERT OR IGNORE INTO one_time_prekeys (public_key, secret_key) VALUES (?, ?);");
  bindBlob(s, 1, publicKey.data(), publicKey.size());
  bindBlob(s, 2, secretKey.data(), secretKey.size());
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: saveOneTimePrekey fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
}

size_t LocalStore::countOneTimePrekeys() {
  Stmt s(db_, "SELECT COUNT(*) FROM one_time_prekeys;");
  if (sqlite3_step(s) != SQLITE_ROW) return 0;
  return static_cast<size_t>(sqlite3_column_int64(s, 0));
}

std::vector<Bytes> LocalStore::listOneTimePrekeyPublics() {
  Stmt s(db_, "SELECT public_key FROM one_time_prekeys;");
  std::vector<Bytes> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnBlob(s, 0));
  }
  return out;
}

std::optional<Bytes> LocalStore::takeOneTimePrekeySecret(const Bytes& publicKey) {
  Bytes secret;
  {
    Stmt s(db_, "SELECT secret_key FROM one_time_prekeys WHERE public_key = ?;");
    bindBlob(s, 1, publicKey.data(), publicKey.size());
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    secret = columnBlob(s, 0);
  }

  Stmt del(db_, "DELETE FROM one_time_prekeys WHERE public_key = ?;");
  bindBlob(del, 1, publicKey.data(), publicKey.size());
  if (sqlite3_step(del) != SQLITE_DONE) {
    throw std::runtime_error(std::string("LocalStore: takeOneTimePrekeySecret fallo: ") +
                             sqlite3_errmsg(db_));
  }
  persistToDisk();
  return secret;
}

}  // namespace templar::client
