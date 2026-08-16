// Reproduce el bug reportado: cerrar la ventana de un usuario y volver a
// abrirla, y comprobar que (a) una conversacion YA establecida sigue
// funcionando, y (b) alguien puede iniciar una conversacion NUEVA con ese
// usuario despues del reinicio (antes de este fix, su identidad quedaba
// "huerfana" en el servidor).
//
// No necesita red: opera directamente sobre LocalStore + CryptoEngine, las
// mismas clases que usa MainWindow, simulando "cerrar la app" con destruir
// y reconstruir esos objetos dentro del mismo proceso de test.

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include <iostream>
#include <stdexcept>

#include "CryptoEngine.hpp"
#include "LocalStore.hpp"
#include "MessagePayload.hpp"
#include "templar/crypto/Identity.hpp"

using namespace templar::client;
using namespace templar::crypto;

namespace {

void check(bool cond, const char* what) {
  if (!cond) throw std::runtime_error(std::string("FALLO: ") + what);
}

std::string storeFilePath(const std::string& username) {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return (dir + "/" + QString::fromStdString(username) + ".tdb").toStdString();
}

PersistedIdentity snapshotIdentity(const CryptoEngine& c) {
  PersistedIdentity id;
  id.ed = c.identity().signingKeys();
  id.x = c.identity().exchangeKeys();
  id.signedPrekey = c.signedPrekey();
  id.signedPrekeySig = c.signedPrekeySignature();
  return id;
}

PrekeyBundle bundleFromIdentity(const PersistedIdentity& id) {
  PrekeyBundle bundle{};
  std::memcpy(bundle.identityPkX25519, id.x.pk, crypto_box_PUBLICKEYBYTES);
  std::memcpy(bundle.identityPkEd25519, id.ed.pk, crypto_sign_PUBLICKEYBYTES);
  std::memcpy(bundle.signedPrekeyPub, id.signedPrekey.pk, crypto_box_PUBLICKEYBYTES);
  bundle.signedPrekeySig = id.signedPrekeySig;
  return bundle;
}

}  // namespace

int main(int argc, char* argv[]) {
  initSodium();
  QCoreApplication app(argc, argv);

  const std::string aliceUser = "persist_test_alice";
  const std::string bobUser = "persist_test_bob";
  const std::string carolUser = "persist_test_carol";
  const std::string password = "password123";

  // Limpieza de restos de una ejecucion anterior fallida.
  QFile::remove(QString::fromStdString(storeFilePath(aliceUser)));
  QFile::remove(QString::fromStdString(storeFilePath(bobUser)));

  try {
    // --- "Primera sesion": Alice y Bob arrancan, se registran (simulado) y
    // hablan ---
    CryptoEngine aliceCrypto;
    LocalStore aliceStore;
    check(aliceStore.unlock(aliceUser, password) == UnlockResult::CreatedNew,
         "primer unlock de alice debe crear un almacen nuevo");
    aliceStore.saveIdentity(snapshotIdentity(aliceCrypto));

    CryptoEngine bobCrypto1;
    LocalStore bobStore1;
    check(bobStore1.unlock(bobUser, password) == UnlockResult::CreatedNew,
         "primer unlock de bob debe crear un almacen nuevo");
    PersistedIdentity bobOriginalIdentity = snapshotIdentity(bobCrypto1);
    bobStore1.saveIdentity(bobOriginalIdentity);

    // Esto es lo que el SERVIDOR guardaria de Bob en el registro -- su
    // identidad de ESTA primera sesion. Debe seguir siendo valida despues
    // de que Bob "reinicie" el cliente para que el fix cuente como tal.
    PrekeyBundle bobBundleAsRegistered = bundleFromIdentity(bobOriginalIdentity);

    OutboundFirstMessage first = aliceCrypto.encryptFirst(
        bobUser, bobBundleAsRegistered, MessagePayload::encodeText("hola bob, soy alice"));
    aliceStore.saveSession(bobUser, aliceCrypto.exportSession(bobUser));

    std::string bobGot =
        MessagePayload::decode(bobCrypto1.decryptFirst(aliceUser, first.senderIdentityPkX25519,
                                                       first.senderEphemeralPk, first.ciphertext,
                                                       /*usedOneTimePrekey=*/nullptr))
            .text;
    check(bobGot == "hola bob, soy alice", "bob debe descifrar el primer mensaje de alice");
    bobStore1.saveSession(aliceUser, bobCrypto1.exportSession(aliceUser));

    Bytes reply = bobCrypto1.encryptNext(aliceUser, MessagePayload::encodeText("hola alice, soy bob"));
    bobStore1.saveSession(aliceUser, bobCrypto1.exportSession(aliceUser));

    std::string aliceGot = MessagePayload::decode(aliceCrypto.decryptNext(bobUser, reply)).text;
    check(aliceGot == "hola alice, soy bob", "alice debe descifrar la respuesta de bob");
    aliceStore.saveSession(bobUser, aliceCrypto.exportSession(bobUser));

    std::cout << "[OK]   primera_sesion_intercambio_normal\n";

    // --- Bob "cierra la ventana" (destruye sus objetos en memoria) ---
    bobStore1.lock();
    // bobCrypto1/bobStore1 salen de scope aqui abajo; en su lugar,
    // reconstruimos como si el proceso hubiera arrancado de cero.

    // --- Bob "vuelve a abrir la app" ---
    CryptoEngine bobCrypto2;  // genera una identidad NUEVA, como hacia siempre hasta este fix
    LocalStore bobStore2;
    UnlockResult secondUnlock = bobStore2.unlock(bobUser, password);
    check(secondUnlock == UnlockResult::LoadedExisting,
         "el segundo unlock de bob debe encontrar el almacen ya creado");

    auto restoredIdentity = bobStore2.loadIdentity();
    check(restoredIdentity.has_value(), "debe haber una identidad persistida para bob");
    Identity bobIdentity2(restoredIdentity->ed, restoredIdentity->x);
    bobCrypto2.restoreIdentity(bobIdentity2, restoredIdentity->signedPrekey,
                               restoredIdentity->signedPrekeySig);

    for (const std::string& peer : bobStore2.listSessionPeers()) {
      auto session = bobStore2.loadSession(peer);
      if (session) bobCrypto2.importSession(peer, *session);
    }

    // Check (a): la conversacion YA establecida con alice debe seguir
    // funcionando -- alice (que nunca "cerro" nada) manda otro mensaje mas
    // reusando su sesion existente.
    Bytes aliceMsg2 = aliceCrypto.encryptNext(bobUser, MessagePayload::encodeText("sigues ahi, bob?"));
    aliceStore.saveSession(bobUser, aliceCrypto.exportSession(bobUser));

    std::string bobGot2 = MessagePayload::decode(bobCrypto2.decryptNext(aliceUser, aliceMsg2)).text;
    check(bobGot2 == "sigues ahi, bob?",
         "bob (reiniciado) debe poder seguir descifrando la conversacion existente con alice");

    std::cout << "[OK]   conversacion_existente_sobrevive_reinicio\n";

    // Check (b): alguien que NUNCA hablo con bob antes debe poder
    // iniciar una conversacion nueva con el DESPUES del reinicio, usando el
    // bundle que el servidor tiene guardado desde el registro original --
    // esto es lo que estaba roto: la identidad de bob quedaba huerfana.
    CryptoEngine carolCrypto;
    OutboundFirstMessage carolFirst = carolCrypto.encryptFirst(
        bobUser, bobBundleAsRegistered, MessagePayload::encodeText("hola bob, soy carol"));

    std::string bobGot3 = MessagePayload::decode(bobCrypto2.decryptFirst(
                                                      carolUser, carolFirst.senderIdentityPkX25519,
                                                      carolFirst.senderEphemeralPk,
                                                      carolFirst.ciphertext,
                                                      /*usedOneTimePrekey=*/nullptr))
                             .text;
    check(bobGot3 == "hola bob, soy carol",
         "bob (reiniciado) debe poder recibir una conversacion NUEVA usando la identidad "
         "que el servidor ya tenia registrada");

    std::cout << "[OK]   conversacion_nueva_tras_reinicio_con_identidad_estable\n";

  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    QFile::remove(QString::fromStdString(storeFilePath(aliceUser)));
    QFile::remove(QString::fromStdString(storeFilePath(bobUser)));
    return 1;
  }

  QFile::remove(QString::fromStdString(storeFilePath(aliceUser)));
  QFile::remove(QString::fromStdString(storeFilePath(bobUser)));

  std::cout << "Todos los tests de persistencia pasaron.\n";
  return 0;
}
