// Reproduce el escenario exacto que reporto el usuario: un usuario que ya
// tenia un almacen local (de una sesion contra un servidor anterior) se
// re-registra contra un servidor DISTINTO (p.ej. porque se reseteo la base
// de datos del servidor) con el mismo nombre de cuenta. Antes del fix, al
// iniciar sesion se restauraba la identidad VIEJA del almacen local en vez
// de la recien registrada, y nadie podia completar el intercambio de
// claves con ese usuario -- ni conversaciones viejas ni nuevas.
//
// Requiere DOS templar_server corriendo (representan "servidor viejo" y
// "servidor nuevo"), puertos en argv[1] y argv[2].

#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <QTextEdit>

#include <iostream>
#include <stdexcept>

#include "MainWindow.hpp"
#include "templar/crypto/Identity.hpp"

using namespace templar::client;

namespace {

void check(bool cond, const char* what) {
  if (!cond) throw std::runtime_error(std::string("FALLO: ") + what);
}

template <typename T>
T* find(QWidget* root, const char* name) {
  T* w = root->findChild<T*>(name);
  if (!w) throw std::runtime_error(std::string("No se encontro el widget: ") + name);
  return w;
}

void selectConversationByName(QListWidget* list, const QString& peer) {
  for (int i = 0; i < list->count(); ++i) {
    QListWidgetItem* item = list->item(i);
    if (item->data(Qt::UserRole).toString() == peer) {
      list->setCurrentItem(item);
      return;
    }
  }
  throw std::runtime_error(("No aparecio '" + peer + "' en la barra lateral").toStdString());
}

QString storeFilePath(const QString& username) {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return dir + "/" + username + ".tdb";
}

void connectRegisterLogin(MainWindow& w, const QString& port, const QString& user,
                          const QString& pass) {
  find<QLineEdit>(&w, "hostEdit")->setText("127.0.0.1");
  find<QLineEdit>(&w, "portEdit")->setText(port);
  find<QPushButton>(&w, "connectButton")->click();
  QTest::qWait(400);

  find<QLineEdit>(&w, "usernameEdit")->setText(user);
  find<QLineEdit>(&w, "passwordEdit")->setText(pass);
  find<QPushButton>(&w, "registerButton")->click();
  QTest::qWait(400);

  find<QPushButton>(&w, "loginButton")->click();
  QTest::qWait(400);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Uso: " << argv[0] << " <puerto_servidor_viejo> <puerto_servidor_nuevo>\n";
    return 2;
  }
  templar::crypto::initSodium();

  QApplication app(argc, argv);
  QString oldPort = QString::fromStdString(argv[1]);
  QString newPort = QString::fromStdString(argv[2]);

  // Limpieza de restos de una ejecucion anterior fallida.
  QFile::remove(storeFilePath("rereg_alice"));
  QFile::remove(storeFilePath("rereg_carol"));

  try {
    // --- Fase 1: "servidor viejo" -- alice crea su cuenta y su almacen local ---
    {
      MainWindow aliceOld;
      connectRegisterLogin(aliceOld, oldPort, "rereg_alice", "password123");
      find<QPushButton>(&aliceOld, "disconnectButton")->click();
      QTest::qWait(300);
      // aliceOld se destruye aqui al salir del scope -- simula cerrar la app.
    }

    // --- Fase 2: "servidor nuevo" (p.ej. base de datos reseteada) -- alice
    // se re-registra con el MISMO nombre de usuario, y carol es una cuenta
    // nueva que nunca hablo con ella. El almacen local de la fase 1 sigue
    // en disco (mismo username = mismo archivo) cuando esto ocurre.
    MainWindow aliceNew;
    MainWindow carol;

    connectRegisterLogin(aliceNew, newPort, "rereg_alice", "password123");
    connectRegisterLogin(carol, newPort, "rereg_carol", "password123");

    find<QLineEdit>(&carol, "newChatPeerEdit")->setText("rereg_alice");
    find<QPushButton>(&carol, "newChatButton")->click();
    QTest::qWait(200);

    find<QLineEdit>(&carol, "messageEdit")->setText("hola alice, soy carol (servidor nuevo)");
    find<QPushButton>(&carol, "sendButton")->click();
    QTest::qWait(1500);

    auto* aliceList = find<QListWidget>(&aliceNew, "conversationList");
    selectConversationByName(aliceList, "rereg_carol");
    QTest::qWait(200);

    auto* aliceChatView = find<QTextEdit>(&aliceNew, "chatView");
    check(aliceChatView->toPlainText().contains("hola alice, soy carol (servidor nuevo)"),
         "alice (re-registrada) debe poder recibir un mensaje de una cuenta nueva en el "
         "servidor nuevo, a pesar de tener un almacen local viejo de otro servidor");

    std::cout << "[OK] Re-registro con almacen local viejo: mensaje recibido correctamente.\n";
  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    QFile::remove(storeFilePath("rereg_alice"));
    QFile::remove(storeFilePath("rereg_carol"));
    return 1;
  }

  QFile::remove(storeFilePath("rereg_alice"));
  QFile::remove(storeFilePath("rereg_carol"));
  return 0;
}
