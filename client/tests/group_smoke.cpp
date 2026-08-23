// Flujo completo de grupos via GUI real (los mismos widgets/slots que usa
// una persona), sobre el mismo patron que gui_smoke.cpp: crear un grupo,
// invitar, aceptar la invitacion desde el panel no-modal de la barra
// lateral, mandar un mensaje de grupo en cada direccion (fan-out sobre los
// canales 1-a-1 existentes, sin conocimiento del servidor del contenido),
// expulsar a un miembro, volver a anadirlo, y que se vaya el solo.
//
// Requiere un templar_server real corriendo, puerto pasado en argv[1].

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTest>
#include <QTextEdit>
#include <QTimer>

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

QListWidgetItem* findConversationItem(QListWidget* list, const QString& textPrefix) {
  for (int i = 0; i < list->count(); ++i) {
    QListWidgetItem* item = list->item(i);
    if (item->text() == textPrefix || item->text().startsWith(textPrefix + " (")) return item;
  }
  return nullptr;
}

// Acepta la primera invitacion pendiente del panel no-modal de la barra
// lateral (inviteList_/acceptInviteButton_) -- ya no hace falta lidiar con
// ningun dialogo modal para esto, a diferencia de antes: la invitacion se
// queda quieta en la UI hasta que se la atiende, sin interrumpir nada.
void acceptFirstPendingInvite(MainWindow& w) {
  auto* inviteList = find<QListWidget>(&w, "inviteList");
  check(inviteList->count() > 0, "debe haber una invitacion pendiente en el panel");
  inviteList->setCurrentRow(0);
  find<QPushButton>(&w, "acceptInviteButton")->click();
}

// Rellena y acepta el primer QInputDialog modal que aparezca -- se usa
// tanto para elegir a quien expulsar (lista) como para escribir a quien
// anadir (texto libre): QInputDialog::getItem/getText comparten la misma
// clase por debajo, y setTextValue() funciona igual en ambos modos.
void fillAndAcceptNextInputDialog(const QString& value, int delayMs = 300) {
  QTimer::singleShot(delayMs, [value] {
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QInputDialog*>(w);
      if (!box) continue;
      box->setTextValue(value);
      box->accept();
      return;
    }
  });
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: " << argv[0] << " <puerto>\n";
    return 2;
  }
  templar::crypto::initSodium();

  QApplication app(argc, argv);
  QString port = QString::fromStdString(argv[1]);
  // Sufijo unico por ejecucion: si el test se corre varias veces contra el
  // mismo templar_server/DB de pruebas sin reiniciarlo (a diferencia de
  // gui_smoke, aqui el nombre de grupo no es una clave unica como un
  // username), un nombre fijo colisionaria con grupos de ejecuciones
  // anteriores y la barra lateral tendria varias entradas con el mismo
  // texto, rompiendo la busqueda por nombre.
  const QString kGroupName = QString("Templarios_%1").arg(QDateTime::currentMSecsSinceEpoch());

  MainWindow alice;
  MainWindow bob;
  alice.show();
  bob.show();

  try {
    // --- Conectar, registrar, iniciar sesion ---
    for (MainWindow* w : {&alice, &bob}) {
      find<QLineEdit>(w, "hostEdit")->setText("127.0.0.1");
      find<QLineEdit>(w, "portEdit")->setText(port);
      find<QPushButton>(w, "connectButton")->click();
    }
    QTest::qWait(500);

    find<QLineEdit>(&alice, "usernameEdit")->setText("grp_alice");
    find<QLineEdit>(&alice, "passwordEdit")->setText("password123");
    find<QPushButton>(&alice, "registerButton")->click();

    find<QLineEdit>(&bob, "usernameEdit")->setText("grp_bob");
    find<QLineEdit>(&bob, "passwordEdit")->setText("password123");
    find<QPushButton>(&bob, "registerButton")->click();
    QTest::qWait(500);

    find<QPushButton>(&alice, "loginButton")->click();
    find<QPushButton>(&bob, "loginButton")->click();
    QTest::qWait(500);

    // --- Alice y bob se conocen primero por chat 1-a-1 (para que bob
    // aparezca como contacto invitable en el dialogo de crear grupo) ---
    find<QLineEdit>(&alice, "newChatPeerEdit")->setText("grp_bob");
    find<QPushButton>(&alice, "newChatButton")->click();
    QTest::qWait(200);
    find<QLineEdit>(&alice, "messageEdit")->setText("hola antes de crear el grupo");
    find<QPushButton>(&alice, "sendButton")->click();
    QTest::qWait(800);

    // --- Alice crea el grupo con bob invitado desde el dialogo real
    // (CreateGroupDialog), sin saltarse la UI ---
    QTimer::singleShot(300, [kGroupName] {
      // El dialogo modal (CreateGroupDialog) aparece como uno de los
      // widgets de nivel superior mientras esta abierto -- MainWindow no
      // es un QDialog, asi que qobject_cast ya descarta alice/bob solo.
      for (QWidget* w : QApplication::topLevelWidgets()) {
        auto* d = qobject_cast<QDialog*>(w);
        if (!d) continue;
        auto* nameEdit = d->findChild<QLineEdit*>("groupNameEdit");
        auto* contactsList = d->findChild<QListWidget*>("groupContactsList");
        auto* buttons = d->findChild<QDialogButtonBox*>("groupDialogButtons");
        if (!nameEdit || !contactsList || !buttons) continue;

        nameEdit->setText(kGroupName);
        for (int i = 0; i < contactsList->count(); ++i) {
          QListWidgetItem* item = contactsList->item(i);
          if (item->text() == "grp_bob") item->setCheckState(Qt::Checked);
        }
        buttons->button(QDialogButtonBox::Ok)->click();
        return;
      }
    });
    find<QPushButton>(&alice, "createGroupButton")->click();
    QTest::qWait(1000);

    auto* aliceList = find<QListWidget>(&alice, "conversationList");
    QListWidgetItem* groupInAlice = findConversationItem(aliceList, kGroupName);
    check(groupInAlice != nullptr, "el grupo debe aparecer en la barra de alice");
    std::cout << "[OK] Grupo creado y visible en la barra lateral de alice.\n";

    // --- La invitacion de bob aparece en su panel no-modal, no en un
    // dialogo encima de lo que estuviera haciendo -- se acepta desde ahi ---
    acceptFirstPendingInvite(bob);
    QTest::qWait(800);

    auto* bobList = find<QListWidget>(&bob, "conversationList");
    QListWidgetItem* groupInBob = findConversationItem(bobList, kGroupName);
    check(groupInBob != nullptr, "el grupo debe aparecer en la barra de bob tras aceptar");
    std::cout << "[OK] Bob acepto la invitacion (panel no-modal) y ve el grupo.\n";

    // --- Alice manda un mensaje al grupo ---
    aliceList->setCurrentItem(groupInAlice);
    QTest::qWait(100);
    find<QLineEdit>(&alice, "messageEdit")->setText("hola grupo, soy alice");
    find<QPushButton>(&alice, "sendButton")->click();
    QTest::qWait(1000);

    bobList->setCurrentItem(groupInBob);
    QTest::qWait(100);
    auto* bobChatView = find<QTextEdit>(&bob, "chatView");
    check(bobChatView->toPlainText().contains("hola grupo, soy alice"),
         "bob debe recibir el mensaje de grupo de alice");
    std::cout << "[OK] Bob recibio el mensaje de grupo de alice (fan-out 1-a-1).\n";

    // --- Bob responde al grupo ---
    find<QLineEdit>(&bob, "messageEdit")->setText("hola alice, aqui bob en el grupo");
    find<QPushButton>(&bob, "sendButton")->click();
    QTest::qWait(1000);

    auto* aliceChatView = find<QTextEdit>(&alice, "chatView");
    check(aliceChatView->toPlainText().contains("hola alice, aqui bob en el grupo"),
         "alice debe recibir la respuesta de grupo de bob");
    std::cout << "[OK] Alice recibio la respuesta de grupo de bob.\n";

    // --- Alice (admin) expulsa a bob ---
    fillAndAcceptNextInputDialog("grp_bob");
    find<QPushButton>(&alice, "kickButton")->click();
    QTest::qWait(800);

    check(findConversationItem(bobList, kGroupName) == nullptr,
         "tras ser expulsado, el grupo debe desaparecer de la barra lateral de bob");
    std::cout << "[OK] Bob fue expulsado del grupo y ya no lo ve en su barra lateral.\n";

    // --- Alice vuelve a anadir a bob con el boton de anadir miembro ---
    aliceList->setCurrentItem(groupInAlice);
    QTest::qWait(100);
    fillAndAcceptNextInputDialog("grp_bob");
    find<QPushButton>(&alice, "addMemberButton")->click();
    QTest::qWait(800);

    acceptFirstPendingInvite(bob);
    QTest::qWait(800);

    check(findConversationItem(bobList, kGroupName) != nullptr,
         "bob debe volver a ver el grupo tras ser anadido de nuevo");
    std::cout << "[OK] Alice anadio a bob de nuevo y bob acepto.\n";

    // --- Bob decide salir del grupo por su cuenta ---
    bobList->setCurrentItem(findConversationItem(bobList, kGroupName));
    QTest::qWait(100);
    find<QPushButton>(&bob, "leaveGroupButton")->click();
    QTest::qWait(800);

    check(findConversationItem(bobList, kGroupName) == nullptr,
         "tras salir por su cuenta, el grupo debe desaparecer de la barra lateral de bob");
    check(aliceChatView->toPlainText().contains("salio del grupo") ||
             find<QTextEdit>(&alice, "chatView")->toPlainText().contains("salio del grupo"),
         "alice debe ver en el chat que bob salio del grupo");
    std::cout << "[OK] Bob salio del grupo por su cuenta y alice se entero.\n";

    // --- Bug real reportado por el usuario: un grupo del que ya no soy
    // miembro (aqui, porque me sali; expulsado o grupo borrado siguen
    // exactamente el mismo camino en el cliente) reaparecia como un chat
    // fantasma al reconectar -- id en crudo como nombre y con punto de
    // presencia, porque el historial local seguia en LocalStore pero el
    // cliente no tenia forma de saber que esa clave habia sido un grupo
    // una vez perdida la membresia (ver LocalStore::rememberGroupKey /
    // loadKnownGroupKeys y knownGroupKeys_ en MainWindow). Se simula
    // "cerrar y reabrir la app" con una instancia NUEVA de MainWindow para
    // bob -- mismo LocalStore en disco, mismo username -- en vez de
    // reusar el objeto ya conectado.
    MainWindow bobReconnected;
    bobReconnected.show();
    find<QLineEdit>(&bobReconnected, "hostEdit")->setText("127.0.0.1");
    find<QLineEdit>(&bobReconnected, "portEdit")->setText(port);
    find<QPushButton>(&bobReconnected, "connectButton")->click();
    QTest::qWait(500);

    find<QLineEdit>(&bobReconnected, "usernameEdit")->setText("grp_bob");
    find<QLineEdit>(&bobReconnected, "passwordEdit")->setText("password123");
    find<QPushButton>(&bobReconnected, "loginButton")->click();
    QTest::qWait(800);

    auto* bobReconnectedList = find<QListWidget>(&bobReconnected, "conversationList");
    check(findConversationItem(bobReconnectedList, kGroupName) == nullptr,
         "el grupo abandonado no debe reaparecer con su nombre tras reconectar");
    // Solo deben quedar "Sistema", "Tu" (chat contigo mismo, siempre
    // presente tras el login) y la conversacion 1-a-1 con alice -- ninguna
    // entrada extra (el fantasma apareceria aqui con el id en crudo).
    check(bobReconnectedList->count() == 3,
         "tras reconectar, bob solo debe ver 'Sistema', 'Tu' y 'grp_alice' -- ninguna entrada "
         "fantasma del grupo abandonado");
    std::cout << "[OK] Tras reconectar, el grupo abandonado no reaparece como chat fantasma.\n";

  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    return 1;
  }

  return 0;
}
