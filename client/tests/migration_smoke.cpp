// Verifica que un almacen local creado con el esquema VIEJO de `history`
// (anterior a la Fase 4: html_line en vez de kind/who/text) se migra
// correctamente al desbloquearlo -- sin lanzar, y sin dejar inutilizable el
// resto del almacen (identidad, sesiones). Reproduce el bug reportado: el
// chat dejaba de funcionar tras la Fase 4 para cuentas con almacen previo.

#include <sodium.h>
#include <sqlite3.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QStandardPaths>

#include <iostream>
#include <stdexcept>
#include <vector>

#include "LocalStore.hpp"
#include "templar/crypto/Identity.hpp"

using namespace templar::client;
using namespace templar::crypto;

namespace {

void check(bool cond, const char* what) {
  if (!cond) throw std::runtime_error(std::string("FALLO: ") + what);
}

std::string storeFilePath(const std::string& username) {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return (dir + "/" + QString::fromStdString(username) + ".tdb").toStdString();
}

// Construye a mano un archivo de almacen con el mismo sobre cifrado que usa
// LocalStore::persistToDisk, pero con el esquema de `history` de ANTES de
// la Fase 4 -- replica lo que habria dejado en disco una version anterior
// del cliente.
void writeLegacyStore(const std::string& path, const std::string& password) {
  sqlite3* db = nullptr;
  check(sqlite3_open(":memory:", &db) == SQLITE_OK, "sqlite3_open fallo");

  const char* schema =
      "CREATE TABLE identity (id INTEGER PRIMARY KEY CHECK (id=1), ed25519_pk BLOB NOT NULL, "
      "ed25519_sk BLOB NOT NULL, x25519_pk BLOB NOT NULL, x25519_sk BLOB NOT NULL, "
      "signed_prekey_pk BLOB NOT NULL, signed_prekey_sk BLOB NOT NULL, "
      "signed_prekey_sig BLOB NOT NULL);"
      "CREATE TABLE sessions (peer_username TEXT PRIMARY KEY, ratchet_state BLOB NOT NULL);"
      "CREATE TABLE history (id INTEGER PRIMARY KEY AUTOINCREMENT, conversation_key TEXT NOT NULL, "
      "html_line TEXT NOT NULL);";
  char* errMsg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "?";
    sqlite3_free(errMsg);
    throw std::runtime_error("schema legacy fallo: " + err);
  }

  const char* insert =
      "INSERT INTO history (conversation_key, html_line) VALUES ('', "
      "'<i>[SISTEMA] hola desde una version vieja</i>');";
  if (sqlite3_exec(db, insert, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "?";
    sqlite3_free(errMsg);
    throw std::runtime_error("insert legacy fallo: " + err);
  }

  // Identidad "real" (generada con el mismo codigo que usa el cliente) para
  // verificar que sobrevive la migracion -- esa tabla no cambio de esquema.
  Identity identity;
  X25519KeyPair spk = generateX25519KeyPair();
  Bytes spkBytes(spk.pk, spk.pk + crypto_box_PUBLICKEYBYTES);
  Bytes sig = identity.sign(spkBytes);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db,
                     "INSERT INTO identity (id, ed25519_pk, ed25519_sk, x25519_pk, x25519_sk, "
                     "signed_prekey_pk, signed_prekey_sk, signed_prekey_sig) "
                     "VALUES (1, ?, ?, ?, ?, ?, ?, ?);",
                     -1, &stmt, nullptr);
  sqlite3_bind_blob(stmt, 1, identity.signingKeys().pk, crypto_sign_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 2, identity.signingKeys().sk, crypto_sign_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 3, identity.exchangeKeys().pk, crypto_box_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 4, identity.exchangeKeys().sk, crypto_box_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 5, spk.pk, crypto_box_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 6, spk.sk, crypto_box_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 7, sig.data(), static_cast<int>(sig.size()), SQLITE_TRANSIENT);
  check(sqlite3_step(stmt) == SQLITE_DONE, "insert identity legacy fallo");
  sqlite3_finalize(stmt);

  sqlite3_int64 size = 0;
  unsigned char* serialized = sqlite3_serialize(db, "main", &size, 0);
  check(serialized != nullptr, "sqlite3_serialize fallo");

  // --- Envolver exactamente como LocalStore::persistToDisk ---
  unsigned char salt[crypto_pwhash_SALTBYTES];
  randombytes_buf(salt, sizeof(salt));
  unsigned char key[32];
  check(crypto_pwhash(key, sizeof(key), password.c_str(), password.size(), salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) == 0,
       "crypto_pwhash fallo");

  unsigned char nonce[crypto_aead_xchacha20poly1305_IETF_NPUBBYTES];
  randombytes_buf(nonce, sizeof(nonce));

  std::vector<unsigned char> ciphertext(size + crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long ciphertextLen = 0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertextLen, serialized, size,
                                             nullptr, 0, nullptr, nonce, key);
  ciphertext.resize(ciphertextLen);
  sqlite3_free(serialized);
  sqlite3_close(db);

  QFile file(QString::fromStdString(path));
  check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "no se pudo escribir el archivo legacy");
  file.write("TDR1", 4);
  file.write(reinterpret_cast<const char*>(salt), sizeof(salt));
  file.write(reinterpret_cast<const char*>(nonce), sizeof(nonce));
  file.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<qint64>(ciphertext.size()));
  file.close();
}

// Igual que writeLegacyStore, pero con el esquema INTERMEDIO: ya tiene
// kind/who/text (Fase 4) pero todavia no tiene created_at (anadido despues,
// para mostrar la hora de cada mensaje). Este caso debe migrar SIN perder
// los mensajes existentes -- a diferencia del esquema viejo de html_line,
// que no se puede recuperar.
void writeStoreWithoutCreatedAt(const std::string& path, const std::string& password) {
  sqlite3* db = nullptr;
  check(sqlite3_open(":memory:", &db) == SQLITE_OK, "sqlite3_open fallo");

  const char* schema =
      "CREATE TABLE identity (id INTEGER PRIMARY KEY CHECK (id=1), ed25519_pk BLOB NOT NULL, "
      "ed25519_sk BLOB NOT NULL, x25519_pk BLOB NOT NULL, x25519_sk BLOB NOT NULL, "
      "signed_prekey_pk BLOB NOT NULL, signed_prekey_sk BLOB NOT NULL, "
      "signed_prekey_sig BLOB NOT NULL);"
      "CREATE TABLE sessions (peer_username TEXT PRIMARY KEY, ratchet_state BLOB NOT NULL);"
      "CREATE TABLE history (id INTEGER PRIMARY KEY AUTOINCREMENT, conversation_key TEXT NOT NULL, "
      "kind INTEGER NOT NULL, who TEXT NOT NULL, text TEXT NOT NULL);";
  char* errMsg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "?";
    sqlite3_free(errMsg);
    throw std::runtime_error("schema intermedio fallo: " + err);
  }

  const char* insert =
      "INSERT INTO history (conversation_key, kind, who, text) VALUES "
      "('bob', 1, 'alice', 'mensaje de antes de tener hora');";
  if (sqlite3_exec(db, insert, nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string err = errMsg ? errMsg : "?";
    sqlite3_free(errMsg);
    throw std::runtime_error("insert intermedio fallo: " + err);
  }

  Identity identity;
  X25519KeyPair spk = generateX25519KeyPair();
  Bytes spkBytes(spk.pk, spk.pk + crypto_box_PUBLICKEYBYTES);
  Bytes sig = identity.sign(spkBytes);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db,
                     "INSERT INTO identity (id, ed25519_pk, ed25519_sk, x25519_pk, x25519_sk, "
                     "signed_prekey_pk, signed_prekey_sk, signed_prekey_sig) "
                     "VALUES (1, ?, ?, ?, ?, ?, ?, ?);",
                     -1, &stmt, nullptr);
  sqlite3_bind_blob(stmt, 1, identity.signingKeys().pk, crypto_sign_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 2, identity.signingKeys().sk, crypto_sign_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 3, identity.exchangeKeys().pk, crypto_box_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 4, identity.exchangeKeys().sk, crypto_box_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 5, spk.pk, crypto_box_PUBLICKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 6, spk.sk, crypto_box_SECRETKEYBYTES, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 7, sig.data(), static_cast<int>(sig.size()), SQLITE_TRANSIENT);
  check(sqlite3_step(stmt) == SQLITE_DONE, "insert identity intermedio fallo");
  sqlite3_finalize(stmt);

  sqlite3_int64 size = 0;
  unsigned char* serialized = sqlite3_serialize(db, "main", &size, 0);
  check(serialized != nullptr, "sqlite3_serialize fallo");

  unsigned char salt[crypto_pwhash_SALTBYTES];
  randombytes_buf(salt, sizeof(salt));
  unsigned char key[32];
  check(crypto_pwhash(key, sizeof(key), password.c_str(), password.size(), salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) == 0,
       "crypto_pwhash fallo");

  unsigned char nonce[crypto_aead_xchacha20poly1305_IETF_NPUBBYTES];
  randombytes_buf(nonce, sizeof(nonce));

  std::vector<unsigned char> ciphertext(size + crypto_aead_xchacha20poly1305_IETF_ABYTES);
  unsigned long long ciphertextLen = 0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext.data(), &ciphertextLen, serialized, size,
                                             nullptr, 0, nullptr, nonce, key);
  ciphertext.resize(ciphertextLen);
  sqlite3_free(serialized);
  sqlite3_close(db);

  QFile file(QString::fromStdString(path));
  check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "no se pudo escribir el archivo intermedio");
  file.write("TDR1", 4);
  file.write(reinterpret_cast<const char*>(salt), sizeof(salt));
  file.write(reinterpret_cast<const char*>(nonce), sizeof(nonce));
  file.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<qint64>(ciphertext.size()));
  file.close();
}

}  // namespace

int main(int argc, char* argv[]) {
  initSodium();
  QCoreApplication app(argc, argv);

  const std::string user = "migration_test_user";
  const std::string password = "password123";
  std::string path = storeFilePath(user);
  QFile::remove(QString::fromStdString(path));

  try {
    writeLegacyStore(path, password);

    LocalStore store;
    UnlockResult result = store.unlock(user, password);
    check(result == UnlockResult::LoadedExisting,
         "debe reconocer el archivo legacy como un almacen existente, no crear uno nuevo");

    auto identity = store.loadIdentity();
    check(identity.has_value(),
         "la identidad guardada en el almacen legacy debe sobrevivir a la migracion");

    auto history = store.loadHistory("");
    check(history.empty(),
         "el historial en formato viejo no se puede recuperar, pero debe descartarse limpio "
         "(vacio), no lanzar");

    store.appendHistoryLine("bob", 1, "alice", "hola de nuevo tras la migracion");
    auto reloaded = store.loadHistory("bob");
    check(reloaded.size() == 1 && reloaded[0].text == "hola de nuevo tras la migracion",
         "tras la migracion, guardar y leer historial nuevo debe funcionar con normalidad");

    std::cout << "[OK] migracion de esquema legacy (html_line) verificada correctamente.\n";
  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    QFile::remove(QString::fromStdString(path));
    return 1;
  }
  QFile::remove(QString::fromStdString(path));

  // --- Segundo escenario: falta solo la columna created_at ---
  const std::string user2 = "migration_test_user_2";
  std::string path2 = storeFilePath(user2);
  QFile::remove(QString::fromStdString(path2));

  try {
    writeStoreWithoutCreatedAt(path2, password);

    LocalStore store;
    UnlockResult result = store.unlock(user2, password);
    check(result == UnlockResult::LoadedExisting, "debe reconocer el almacen intermedio existente");

    auto history = store.loadHistory("bob");
    check(history.size() == 1, "el mensaje guardado antes de tener created_at NO debe perderse");
    check(history[0].text == "mensaje de antes de tener hora", "el texto debe sobrevivir intacto");
    check(history[0].createdAt == 0,
         "un mensaje migrado sin hora original debe quedar en 0 (se renderiza sin [HH:MM])");

    store.appendHistoryLine("bob", 1, "alice", "mensaje nuevo, ya con hora");
    auto reloaded = store.loadHistory("bob");
    check(reloaded.size() == 2, "debe poder seguir anadiendo historial tras la migracion");
    check(reloaded[1].createdAt > 0, "un mensaje nuevo tras la migracion debe llevar hora real");

    std::cout << "[OK] migracion anadiendo created_at (sin perder mensajes) verificada.\n";
  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    QFile::remove(QString::fromStdString(path2));
    return 1;
  }

  QFile::remove(QString::fromStdString(path2));
  return 0;
}
