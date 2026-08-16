#include "Database.hpp"

#include <sodium.h>

#include <ctime>
#include <cstdio>
#include <stdexcept>

namespace templar::server {

namespace {

// RAII para sqlite3_stmt*: evita fugas si una excepción salta a mitad de
// una consulta.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("SQLite prepare fallo: ") + sqlite3_errmsg(db));
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

void bindBlob(sqlite3_stmt* s, int idx, const Bytes& v) {
  if (v.empty()) {
    sqlite3_bind_zeroblob(s, idx, 0);
  } else {
    sqlite3_bind_blob(s, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
  }
}

Bytes columnBlob(sqlite3_stmt* s, int idx) {
  const void* data = sqlite3_column_blob(s, idx);
  int len = sqlite3_column_bytes(s, idx);
  if (data == nullptr || len == 0) return {};
  const auto* p = static_cast<const uint8_t*>(data);
  return Bytes(p, p + len);
}

std::string columnText(sqlite3_stmt* s, int idx) {
  const unsigned char* txt = sqlite3_column_text(s, idx);
  int len = sqlite3_column_bytes(s, idx);
  if (txt == nullptr) return {};
  return std::string(reinterpret_cast<const char*>(txt), len);
}

// Id de grupo opaco y no adivinable: 16 bytes aleatorios en hex. No es un
// UUID con formato estandar, pero cumple la misma funcion (identificador
// unico e imposible de enumerar) sin anadir una dependencia nueva.
std::string randomGroupId() {
  unsigned char raw[16];
  randombytes_buf(raw, sizeof(raw));
  char hex[sizeof(raw) * 2 + 1];
  for (size_t i = 0; i < sizeof(raw); ++i) {
    std::snprintf(hex + i * 2, 3, "%02x", raw[i]);
  }
  return std::string(hex, sizeof(raw) * 2);
}

// Mismo criterio que randomGroupId(): id opaco y no adivinable, 16 bytes
// aleatorios en hex. El id de un blob hace de "capability" en si mismo --
// quien lo tiene (solo llega dentro de un mensaje ya cifrado a un
// destinatario concreto) puede descargarlo, no hay una comprobacion de
// permisos aparte para DownloadBlob.
std::string randomBlobId() {
  unsigned char raw[16];
  randombytes_buf(raw, sizeof(raw));
  char hex[sizeof(raw) * 2 + 1];
  for (size_t i = 0; i < sizeof(raw); ++i) {
    std::snprintf(hex + i * 2, 3, "%02x", raw[i]);
  }
  return std::string(hex, sizeof(raw) * 2);
}

bool columnExists(sqlite3* db, const char* table, const char* column) {
  std::string sql = std::string("PRAGMA table_info(") + table + ");";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* name = sqlite3_column_text(stmt, 1);
    if (name && std::string(reinterpret_cast<const char*>(name)) == column) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

}  // namespace

Database::Database(const std::string& path) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    std::string err = db_ ? sqlite3_errmsg(db_) : "desconocido";
    throw std::runtime_error("No se pudo abrir la base de datos: " + err);
  }

  execOrThrow("PRAGMA journal_mode=WAL;");
  execOrThrow("PRAGMA foreign_keys=ON;");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS users ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  username TEXT UNIQUE NOT NULL,"
      "  password_hash TEXT NOT NULL,"
      "  identity_pk_x25519 BLOB NOT NULL,"
      "  identity_pk_ed25519 BLOB NOT NULL,"
      "  signed_prekey_pub BLOB NOT NULL,"
      "  signed_prekey_sig BLOB NOT NULL"
      ");");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS mailbox ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  recipient_id INTEGER NOT NULL REFERENCES users(id),"
      "  sender_username TEXT NOT NULL,"
      "  is_first INTEGER NOT NULL DEFAULT 0,"
      "  sender_identity_pk_x25519 BLOB,"
      "  sender_ephemeral_pk BLOB,"
      "  ciphertext BLOB NOT NULL,"
      "  created_at INTEGER NOT NULL,"
      "  group_id TEXT,"
      "  used_otpk BLOB"
      ");");
  migrateMailboxSchema();

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS one_time_prekeys ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  user_id INTEGER NOT NULL REFERENCES users(id),"
      "  public_key BLOB NOT NULL"
      ");");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS groups ("
      "  id TEXT PRIMARY KEY,"
      "  name TEXT NOT NULL,"
      "  admin_username TEXT NOT NULL,"
      "  created_at INTEGER NOT NULL"
      ");");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS group_members ("
      "  group_id TEXT NOT NULL REFERENCES groups(id),"
      "  username TEXT NOT NULL,"
      "  joined_at INTEGER NOT NULL,"
      "  PRIMARY KEY (group_id, username)"
      ");");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS group_invites ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  group_id TEXT NOT NULL REFERENCES groups(id),"
      "  invitee_username TEXT NOT NULL,"
      "  inviter_username TEXT NOT NULL,"
      "  created_at INTEGER NOT NULL"
      ");");

  execOrThrow(
      "CREATE TABLE IF NOT EXISTS blobs ("
      "  id TEXT PRIMARY KEY,"
      "  owner_username TEXT NOT NULL,"
      "  size INTEGER NOT NULL,"
      "  created_at INTEGER NOT NULL,"
      "  complete INTEGER NOT NULL DEFAULT 0"
      ");");
}

// Bases de datos creadas antes de que existiera group_id no lo tienen
// (CREATE TABLE IF NOT EXISTS no toca tablas ya existentes) -- se anade a
// mano si falta, igual que hace LocalStore::migrateSchema en el cliente.
void Database::migrateMailboxSchema() {
  if (!columnExists(db_, "mailbox", "group_id")) {
    execOrThrow("ALTER TABLE mailbox ADD COLUMN group_id TEXT;");
  }
  if (!columnExists(db_, "mailbox", "used_otpk")) {
    execOrThrow("ALTER TABLE mailbox ADD COLUMN used_otpk BLOB;");
  }
}

Database::~Database() {
  if (db_) sqlite3_close(db_);
}

void Database::execOrThrow(const char* sql) {
  char* errMsg = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "desconocido";
    sqlite3_free(errMsg);
    throw std::runtime_error("SQLite exec fallo (" + std::string(sql) + "): " + err);
  }
}

bool Database::createUser(const std::string& username, const std::string& passwordHash,
                          const Bytes& identityPkX25519, const Bytes& identityPkEd25519,
                          const Bytes& signedPrekeyPub, const Bytes& signedPrekeySig) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "INSERT INTO users (username, password_hash, identity_pk_x25519, "
        "identity_pk_ed25519, signed_prekey_pub, signed_prekey_sig) "
        "VALUES (?, ?, ?, ?, ?, ?);");
  bindText(s, 1, username);
  bindText(s, 2, passwordHash);
  bindBlob(s, 3, identityPkX25519);
  bindBlob(s, 4, identityPkEd25519);
  bindBlob(s, 5, signedPrekeyPub);
  bindBlob(s, 6, signedPrekeySig);

  int rc = sqlite3_step(s);
  if (rc == SQLITE_CONSTRAINT) return false;  // username duplicado (UNIQUE)
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("createUser fallo: ") + sqlite3_errmsg(db_));
  }
  return true;
}

std::optional<Database::UserRecord> Database::findUser(const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "SELECT id, username, password_hash, identity_pk_x25519, identity_pk_ed25519, "
        "signed_prekey_pub, signed_prekey_sig FROM users WHERE username = ?;");
  bindText(s, 1, username);

  if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;

  UserRecord rec;
  rec.id = sqlite3_column_int64(s, 0);
  rec.username = columnText(s, 1);
  rec.passwordHash = columnText(s, 2);
  rec.identityPkX25519 = columnBlob(s, 3);
  rec.identityPkEd25519 = columnBlob(s, 4);
  rec.signedPrekeyPub = columnBlob(s, 5);
  rec.signedPrekeySig = columnBlob(s, 6);
  return rec;
}

int64_t Database::enqueueMessage(const std::string& recipientUsername,
                                 const std::string& senderUsername, bool isFirst,
                                 const Bytes& senderIdentityPkX25519, const Bytes& senderEphemeralPk,
                                 const Bytes& ciphertext, const std::string& groupId,
                                 const Bytes& usedOneTimePrekeyPub) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "INSERT INTO mailbox (recipient_id, sender_username, is_first, "
        "sender_identity_pk_x25519, sender_ephemeral_pk, ciphertext, created_at, group_id, "
        "used_otpk) "
        "SELECT id, ?, ?, ?, ?, ?, ?, ?, ? FROM users WHERE username = ?;");
  bindText(s, 1, senderUsername);
  sqlite3_bind_int(s, 2, isFirst ? 1 : 0);
  bindBlob(s, 3, senderIdentityPkX25519);
  bindBlob(s, 4, senderEphemeralPk);
  bindBlob(s, 5, ciphertext);
  sqlite3_bind_int64(s, 6, static_cast<int64_t>(std::time(nullptr)));
  if (groupId.empty()) {
    sqlite3_bind_null(s, 7);
  } else {
    bindText(s, 7, groupId);
  }
  if (usedOneTimePrekeyPub.empty()) {
    sqlite3_bind_null(s, 8);
  } else {
    bindBlob(s, 8, usedOneTimePrekeyPub);
  }
  bindText(s, 9, recipientUsername);

  int rc = sqlite3_step(s);
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("enqueueMessage fallo: ") + sqlite3_errmsg(db_));
  }
  if (sqlite3_changes(db_) == 0) {
    throw std::runtime_error("enqueueMessage: destinatario '" + recipientUsername +
                             "' no existe");
  }
  return sqlite3_last_insert_rowid(db_);
}

std::vector<Database::MailboxRow> Database::fetchPending(const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "SELECT m.id, m.sender_username, m.is_first, m.sender_identity_pk_x25519, "
        "m.sender_ephemeral_pk, m.ciphertext, m.group_id, m.used_otpk "
        "FROM mailbox m JOIN users u ON m.recipient_id = u.id "
        "WHERE u.username = ? ORDER BY m.id ASC;");
  bindText(s, 1, username);

  std::vector<MailboxRow> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    MailboxRow row;
    row.id = sqlite3_column_int64(s, 0);
    row.senderUsername = columnText(s, 1);
    row.isFirst = sqlite3_column_int(s, 2) != 0;
    row.senderIdentityPkX25519 = columnBlob(s, 3);
    row.senderEphemeralPk = columnBlob(s, 4);
    row.ciphertext = columnBlob(s, 5);
    row.groupId = columnText(s, 6);
    row.usedOneTimePrekeyPub = columnBlob(s, 7);
    out.push_back(std::move(row));
  }
  return out;
}

void Database::ackMessage(int64_t mailboxId, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "DELETE FROM mailbox WHERE id = ? AND recipient_id = "
        "(SELECT id FROM users WHERE username = ?);");
  sqlite3_bind_int64(s, 1, mailboxId);
  bindText(s, 2, username);

  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("ackMessage fallo: ") + sqlite3_errmsg(db_));
  }
}

std::string Database::createGroup(const std::string& name, const std::string& adminUsername) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string id = randomGroupId();
  int64_t now = static_cast<int64_t>(std::time(nullptr));

  {
    Stmt s(db_, "INSERT INTO groups (id, name, admin_username, created_at) VALUES (?, ?, ?, ?);");
    bindText(s, 1, id);
    bindText(s, 2, name);
    bindText(s, 3, adminUsername);
    sqlite3_bind_int64(s, 4, now);
    if (sqlite3_step(s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("createGroup fallo: ") + sqlite3_errmsg(db_));
    }
  }
  {
    Stmt s(db_, "INSERT INTO group_members (group_id, username, joined_at) VALUES (?, ?, ?);");
    bindText(s, 1, id);
    bindText(s, 2, adminUsername);
    sqlite3_bind_int64(s, 3, now);
    if (sqlite3_step(s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("createGroup (miembro admin) fallo: ") +
                               sqlite3_errmsg(db_));
    }
  }
  return id;
}

std::optional<Database::GroupRecord> Database::findGroup(const std::string& groupId) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "SELECT id, name, admin_username FROM groups WHERE id = ?;");
  bindText(s, 1, groupId);
  if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;

  GroupRecord rec;
  rec.id = columnText(s, 0);
  rec.name = columnText(s, 1);
  rec.adminUsername = columnText(s, 2);
  return rec;
}

bool Database::isGroupMember(const std::string& groupId, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "SELECT 1 FROM group_members WHERE group_id = ? AND username = ?;");
  bindText(s, 1, groupId);
  bindText(s, 2, username);
  return sqlite3_step(s) == SQLITE_ROW;
}

void Database::addGroupMember(const std::string& groupId, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "INSERT OR IGNORE INTO group_members (group_id, username, joined_at) VALUES (?, ?, ?);");
  bindText(s, 1, groupId);
  bindText(s, 2, username);
  sqlite3_bind_int64(s, 3, static_cast<int64_t>(std::time(nullptr)));
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("addGroupMember fallo: ") + sqlite3_errmsg(db_));
  }
}

void Database::removeGroupMember(const std::string& groupId, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "DELETE FROM group_members WHERE group_id = ? AND username = ?;");
  bindText(s, 1, groupId);
  bindText(s, 2, username);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("removeGroupMember fallo: ") + sqlite3_errmsg(db_));
  }
}

std::vector<std::string> Database::listGroupMembers(const std::string& groupId) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "SELECT username FROM group_members WHERE group_id = ? ORDER BY joined_at ASC;");
  bindText(s, 1, groupId);

  std::vector<std::string> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnText(s, 0));
  }
  return out;
}

void Database::setGroupAdmin(const std::string& groupId, const std::string& newAdmin) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "UPDATE groups SET admin_username = ? WHERE id = ?;");
  bindText(s, 1, newAdmin);
  bindText(s, 2, groupId);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("setGroupAdmin fallo: ") + sqlite3_errmsg(db_));
  }
}

void Database::deleteGroup(const std::string& groupId) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const char* sql : {"DELETE FROM group_invites WHERE group_id = ?;",
                          "DELETE FROM group_members WHERE group_id = ?;",
                          "DELETE FROM groups WHERE id = ?;"}) {
    Stmt s(db_, sql);
    bindText(s, 1, groupId);
    if (sqlite3_step(s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("deleteGroup fallo: ") + sqlite3_errmsg(db_));
    }
  }
}

std::vector<Database::GroupWithMembers> Database::listGroupsForUser(const std::string& username) {
  std::vector<GroupRecord> groups;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Stmt s(db_,
          "SELECT g.id, g.name, g.admin_username FROM groups g "
          "JOIN group_members gm ON gm.group_id = g.id WHERE gm.username = ?;");
    bindText(s, 1, username);
    while (sqlite3_step(s) == SQLITE_ROW) {
      GroupRecord rec;
      rec.id = columnText(s, 0);
      rec.name = columnText(s, 1);
      rec.adminUsername = columnText(s, 2);
      groups.push_back(std::move(rec));
    }
  }

  std::vector<GroupWithMembers> out;
  out.reserve(groups.size());
  for (auto& g : groups) {
    GroupWithMembers gwm;
    gwm.members = listGroupMembers(g.id);
    gwm.group = std::move(g);
    out.push_back(std::move(gwm));
  }
  return out;
}

int64_t Database::enqueueGroupInvite(const std::string& groupId, const std::string& inviteeUsername,
                                     const std::string& inviterUsername) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "INSERT INTO group_invites (group_id, invitee_username, inviter_username, created_at) "
        "VALUES (?, ?, ?, ?);");
  bindText(s, 1, groupId);
  bindText(s, 2, inviteeUsername);
  bindText(s, 3, inviterUsername);
  sqlite3_bind_int64(s, 4, static_cast<int64_t>(std::time(nullptr)));
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("enqueueGroupInvite fallo: ") + sqlite3_errmsg(db_));
  }
  return sqlite3_last_insert_rowid(db_);
}

std::vector<Database::GroupInviteRow> Database::fetchPendingInvites(const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_,
        "SELECT gi.id, gi.group_id, g.name, gi.inviter_username FROM group_invites gi "
        "JOIN groups g ON g.id = gi.group_id "
        "WHERE gi.invitee_username = ? ORDER BY gi.id ASC;");
  bindText(s, 1, username);

  std::vector<GroupInviteRow> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    GroupInviteRow row;
    row.id = sqlite3_column_int64(s, 0);
    row.groupId = columnText(s, 1);
    row.groupName = columnText(s, 2);
    row.inviterUsername = columnText(s, 3);
    out.push_back(std::move(row));
  }
  return out;
}

std::optional<std::string> Database::resolveInvite(int64_t inviteId, const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string groupId;
  {
    Stmt s(db_, "SELECT group_id FROM group_invites WHERE id = ? AND invitee_username = ?;");
    sqlite3_bind_int64(s, 1, inviteId);
    bindText(s, 2, username);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    groupId = columnText(s, 0);
  }

  Stmt del(db_, "DELETE FROM group_invites WHERE id = ? AND invitee_username = ?;");
  sqlite3_bind_int64(del, 1, inviteId);
  bindText(del, 2, username);
  if (sqlite3_step(del) != SQLITE_DONE) {
    throw std::runtime_error(std::string("resolveInvite fallo: ") + sqlite3_errmsg(db_));
  }
  return groupId;
}

void Database::addOneTimePrekeys(const std::string& username, const std::vector<Bytes>& publicKeys) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt user(db_, "SELECT id FROM users WHERE username = ?;");
  bindText(user, 1, username);
  if (sqlite3_step(user) != SQLITE_ROW) return;  // usuario inexistente -- se ignora en silencio
  int64_t userId = sqlite3_column_int64(user, 0);

  for (const Bytes& pub : publicKeys) {
    Stmt s(db_, "INSERT INTO one_time_prekeys (user_id, public_key) VALUES (?, ?);");
    sqlite3_bind_int64(s, 1, userId);
    bindBlob(s, 2, pub);
    if (sqlite3_step(s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("addOneTimePrekeys fallo: ") + sqlite3_errmsg(db_));
    }
  }
}

std::optional<Bytes> Database::takeOneTimePrekey(const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);

  int64_t id;
  Bytes pub;
  {
    Stmt s(db_,
          "SELECT o.id, o.public_key FROM one_time_prekeys o "
          "JOIN users u ON u.id = o.user_id "
          "WHERE u.username = ? ORDER BY o.id ASC LIMIT 1;");
    bindText(s, 1, username);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    id = sqlite3_column_int64(s, 0);
    pub = columnBlob(s, 1);
  }

  Stmt del(db_, "DELETE FROM one_time_prekeys WHERE id = ?;");
  sqlite3_bind_int64(del, 1, id);
  if (sqlite3_step(del) != SQLITE_DONE) {
    throw std::runtime_error(std::string("takeOneTimePrekey fallo: ") + sqlite3_errmsg(db_));
  }
  return pub;
}

std::string Database::createBlobRecord(const std::string& ownerUsername, uint64_t declaredSize) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string id = randomBlobId();
  Stmt s(db_,
        "INSERT INTO blobs (id, owner_username, size, created_at, complete) "
        "VALUES (?, ?, ?, ?, 0);");
  bindText(s, 1, id);
  bindText(s, 2, ownerUsername);
  sqlite3_bind_int64(s, 3, static_cast<int64_t>(declaredSize));
  sqlite3_bind_int64(s, 4, static_cast<int64_t>(std::time(nullptr)));
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("createBlobRecord fallo: ") + sqlite3_errmsg(db_));
  }
  return id;
}

void Database::markBlobComplete(const std::string& blobId) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "UPDATE blobs SET complete = 1 WHERE id = ?;");
  bindText(s, 1, blobId);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("markBlobComplete fallo: ") + sqlite3_errmsg(db_));
  }
}

std::optional<Database::BlobRecord> Database::findBlob(const std::string& blobId) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "SELECT id, owner_username, size, created_at, complete FROM blobs WHERE id = ?;");
  bindText(s, 1, blobId);
  if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;

  BlobRecord rec;
  rec.id = columnText(s, 0);
  rec.ownerUsername = columnText(s, 1);
  rec.size = static_cast<uint64_t>(sqlite3_column_int64(s, 2));
  rec.createdAt = sqlite3_column_int64(s, 3);
  rec.complete = sqlite3_column_int(s, 4) != 0;
  return rec;
}

std::vector<std::string> Database::listBlobsCreatedBefore(int64_t epochSeconds) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "SELECT id FROM blobs WHERE created_at < ?;");
  sqlite3_bind_int64(s, 1, epochSeconds);

  std::vector<std::string> out;
  while (sqlite3_step(s) == SQLITE_ROW) {
    out.push_back(columnText(s, 0));
  }
  return out;
}

void Database::deleteBlobRecord(const std::string& blobId) {
  std::lock_guard<std::mutex> lock(mutex_);

  Stmt s(db_, "DELETE FROM blobs WHERE id = ?;");
  bindText(s, 1, blobId);
  if (sqlite3_step(s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("deleteBlobRecord fallo: ") + sqlite3_errmsg(db_));
  }
}

}  // namespace templar::server
