// A diferencia de e2e_smoke.cpp (que usa NetworkManager/CryptoEngine
// directamente, sin pasar por MainWindow), este test maneja dos instancias
// reales de MainWindow -- los mismos widgets, los mismos slots, exactamente
// como los usaria una persona haciendo clic. Sirve para atrapar bugs en la
// capa de UI que los tests "por debajo" no pueden ver.
//
// Requiere un templar_server real corriendo, puerto pasado en argv[1].

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
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

QListWidgetItem* findConversationItem(QListWidget* list, const QString& peer) {
  for (int i = 0; i < list->count(); ++i) {
    QListWidgetItem* item = list->item(i);
    if (item->data(Qt::UserRole).toString() == peer) return item;
  }
  return nullptr;
}

void selectConversationByName(QListWidget* list, const QString& peer) {
  QListWidgetItem* item = findConversationItem(list, peer);
  if (!item) throw std::runtime_error(("No aparecio '" + peer + "' en la barra lateral").toStdString());
  list->setCurrentItem(item);
}

// Color del pixel central del punto de presencia -- MainWindow::presenceIcon
// pinta un circulo solido, asi que el centro nunca tiene anti-aliasing.
QColor presenceDotColor(const QListWidgetItem* item) {
  QImage img = item->icon().pixmap(12, 12).toImage();
  return img.pixelColor(6, 6);
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

  MainWindow alice;
  MainWindow bob;
  // Sin mostrar la ventana, QWidget::isVisible() da false para cualquier
  // hijo suyo (la barra de progreso incluida) sin importar que se le llame
  // setVisible(true) -- la visibilidad efectiva depende de toda la cadena
  // de padres. Bajo QT_QPA_PLATFORM=offscreen esto no dibuja nada en una
  // pantalla real, pero mantiene la semantica de visibilidad correcta.
  alice.show();
  bob.show();

  try {
    // --- Conectar ambas ---
    for (MainWindow* w : {&alice, &bob}) {
      find<QLineEdit>(w, "hostEdit")->setText("127.0.0.1");
      find<QLineEdit>(w, "portEdit")->setText(port);
      find<QPushButton>(w, "connectButton")->click();
    }
    QTest::qWait(500);

    // --- Registrar ---
    find<QLineEdit>(&alice, "usernameEdit")->setText("gui_alice");
    find<QLineEdit>(&alice, "passwordEdit")->setText("password123");
    find<QPushButton>(&alice, "registerButton")->click();

    find<QLineEdit>(&bob, "usernameEdit")->setText("gui_bob");
    find<QLineEdit>(&bob, "passwordEdit")->setText("password123");
    find<QPushButton>(&bob, "registerButton")->click();
    QTest::qWait(500);

    // --- Iniciar sesion ---
    find<QPushButton>(&alice, "loginButton")->click();
    find<QPushButton>(&bob, "loginButton")->click();
    QTest::qWait(500);

    check(find<QTextEdit>(&alice, "chatView") != nullptr, "alice debe tener la pantalla de chat");

    // --- Alice abre chat nuevo con bob y le escribe ---
    find<QLineEdit>(&alice, "newChatPeerEdit")->setText("gui_bob");
    find<QPushButton>(&alice, "newChatButton")->click();
    QTest::qWait(200);

    find<QLineEdit>(&alice, "messageEdit")->setText("hola bob desde la gui real");
    find<QPushButton>(&alice, "sendButton")->click();
    QTest::qWait(1500);

    // --- No leido: bob todavia no ha abierto la conversacion (esta viendo
    // "Sistema", la que tiene por defecto), debe verse marcada ---
    auto* bobList = find<QListWidget>(&bob, "conversationList");
    QListWidgetItem* aliceEntryInBob = findConversationItem(bobList, "gui_alice");
    check(aliceEntryInBob != nullptr, "gui_alice debe aparecer en la barra lateral de bob");
    check(aliceEntryInBob->text() == "gui_alice (1)",
         "la entrada debe mostrar el contador de no leidos antes de abrir la conversacion");
    check(aliceEntryInBob->font().bold(), "la entrada debe estar en negrita mientras haya no leidos");
    std::cout << "[OK] Contador de no leidos visible antes de abrir la conversacion.\n";

    // --- Bob selecciona la conversacion con alice: debe limpiarse el no leido ---
    selectConversationByName(bobList, "gui_alice");
    QTest::qWait(200);

    check(aliceEntryInBob->text() == "gui_alice", "al abrir la conversacion el contador debe desaparecer");
    check(!aliceEntryInBob->font().bold(), "al abrir la conversacion debe volver a texto normal");
    std::cout << "[OK] No leido se limpia al abrir la conversacion.\n";

    auto* bobChatView = find<QTextEdit>(&bob, "chatView");
    check(bobChatView->toPlainText().contains("hola bob desde la gui real"),
         "el mensaje de alice debe aparecer en el chat de bob");

    std::cout << "[OK] Bob recibio el mensaje de Alice via GUI real.\n";

    // --- Bob responde ---
    find<QLineEdit>(&bob, "messageEdit")->setText("hola alice, aqui bob");
    find<QPushButton>(&bob, "sendButton")->click();
    QTest::qWait(1500);

    auto* aliceList = find<QListWidget>(&alice, "conversationList");
    selectConversationByName(aliceList, "gui_bob");
    QTest::qWait(200);

    auto* aliceChatView = find<QTextEdit>(&alice, "chatView");
    check(aliceChatView->toPlainText().contains("hola alice, aqui bob"),
         "la respuesta de bob debe aparecer en el chat de alice");

    std::cout << "[OK] Alice recibio la respuesta de Bob via GUI real.\n";

    // --- Selector de emoji: se invoca insertEmoji directamente (el mismo
    // slot al que EmojiPicker::emojiSelected esta conectado) en vez de
    // interactuar con el Qt::Popup real -- mismo motivo que
    // startOutgoingFileTransfer se prueba sin pelear con QFileDialog. Se
    // manda el mensaje resultante para comprobar tambien que el emoji
    // sobrevive intacto al cifrado/descifrado, no solo a la insercion local.
    // A proposito NO lleva "hola" -- la prueba de busqueda de mas abajo
    // cuenta cuantas lineas contienen esa palabra en este mismo chat.
    find<QLineEdit>(&alice, "messageEdit")->setText("genial ");
    check(QMetaObject::invokeMethod(&alice, "insertEmoji", Q_ARG(QString, QString::fromUtf8("🎉"))),
         "no se pudo invocar insertEmoji en alice");
    check(find<QLineEdit>(&alice, "messageEdit")->text() == QString::fromUtf8("genial 🎉"),
         "insertEmoji debe anadir el emoji al final del texto ya escrito");

    find<QPushButton>(&alice, "sendButton")->click();
    QTest::qWait(1000);

    check(bobChatView->toPlainText().contains(QString::fromUtf8("genial 🎉")),
         "el emoji debe llegarle a bob intacto tras cifrar/descifrar");
    std::cout << "[OK] Selector de emoji: insercion y envio cifrado verificados.\n";

    // --- Transferencia de archivo: alice le manda un archivo a bob ---
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString sourceName = "templar_gui_smoke_test_file.bin";
    QString sourcePath = tmpDir + "/" + sourceName;

    // ~2MB (~32 bloques de 64KB): suficiente para ejercitar de verdad la
    // cola de escritura del servidor y detectar si alguna rafaga de
    // bloques seguidos corrompe el framing o tumba la conexion -- un
    // archivo de un solo bloque no habria detectado el bug real que motivo
    // este test (ver Session::doWrite en el servidor).
    constexpr int kTestFileBytes = 2 * 1024 * 1024;
    QByteArray fileContent;
    fileContent.reserve(kTestFileBytes);
    for (int i = 0; i < kTestFileBytes; ++i) {
      fileContent.append(static_cast<char>(i % 251));
    }
    {
      QFile src(sourcePath);
      check(src.open(QIODevice::WriteOnly), "no se pudo crear el archivo de prueba temporal");
      src.write(fileContent);
      src.close();
    }

    // La conversacion activa de alice ya es "gui_bob" (se selecciono arriba).
    // Se llama a startOutgoingFileTransfer directamente (el mismo slot que
    // onAttachClicked usa tras cerrar el QFileDialog) en vez de manejar el
    // dialogo modal real: bajo un entorno sin pantalla de verdad, el
    // QFileDialog no nativo tiene sus propias particularidades de
    // sincronizacion con el modelo de archivos que no tienen nada que ver
    // con la logica de transferencia que este test quiere verificar.
    check(QMetaObject::invokeMethod(&alice, "startOutgoingFileTransfer", Q_ARG(QString, sourcePath)),
         "no se pudo invocar startOutgoingFileTransfer en alice");

    // Se comprueba ANTES de ceder el control al bucle de eventos: justo
    // despues de startOutgoingFileTransfer la barra ya se puso visible de
    // forma sincrona y el primer chunk todavia no se proceso (esta
    // programado para la siguiente vuelta del bucle de eventos), asi que
    // no hay condicion de carrera con lo rapido que pueda ir el envio.
    auto* aliceProgress = find<QProgressBar>(&alice, "transferProgress");
    check(aliceProgress->isVisible(), "la barra de progreso de alice debe mostrarse al enviar");

    QTest::qWait(8000);  // tiempo de sobra para ~32 bloques ida y vuelta

    check(!aliceProgress->isVisible(),
         "la barra de progreso de alice debe ocultarse al terminar el envio");

    QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QString expectedDownloadPath = downloadsDir + "/" + sourceName;
    check(QFile::exists(expectedDownloadPath),
         "el archivo recibido por bob debe existir en su carpeta de Descargas");

    QFile received(expectedDownloadPath);
    check(received.open(QIODevice::ReadOnly), "debe poder abrirse el archivo recibido por bob");
    QByteArray receivedContent = received.readAll();
    received.close();
    check(receivedContent == fileContent,
         "el contenido recibido debe ser byte-a-byte identico al enviado");

    check(bobChatView->toPlainText().contains("recibido y verificado"),
         "el chat de bob debe confirmar que el SHA-256 verifico correctamente");

    std::cout << "[OK] Transferencia de archivo verificada byte a byte, con SHA-256 correcto.\n";

    QFile::remove(sourcePath);
    QFile::remove(expectedDownloadPath);

    // --- Busqueda dentro de la conversacion abierta (alice sigue viendo su
    // chat con bob, que ya tiene "hola bob desde la gui real" y "hola
    // alice, aqui bob" -- dos lineas que contienen "hola") ---
    check(!find<QWidget>(&alice, "searchBarWidget")->isVisible(),
         "la barra de busqueda debe empezar oculta");
    find<QPushButton>(&alice, "searchToggleButton")->click();
    check(find<QWidget>(&alice, "searchBarWidget")->isVisible(),
         "la barra de busqueda debe aparecer al pulsar el boton de buscar");

    auto* searchEdit = find<QLineEdit>(&alice, "searchEdit");
    auto* searchCount = find<QLabel>(&alice, "searchCountLabel");
    searchEdit->setText("hola");
    QTest::qWait(100);
    check(searchCount->text() == "2 resultados",
         "debe contar las 2 lineas que contienen 'hola' en el chat con bob");

    auto* aliceChatViewForSearch = find<QTextEdit>(&alice, "chatView");
    QString firstMatch = aliceChatViewForSearch->textCursor().selectedText();
    int firstMatchPos = aliceChatViewForSearch->textCursor().selectionStart();
    check(firstMatch.compare("hola", Qt::CaseInsensitive) == 0,
         "find() debe dejar seleccionada la primera coincidencia de 'hola'");

    find<QPushButton>(&alice, "searchNextButton")->click();
    QString secondMatch = aliceChatViewForSearch->textCursor().selectedText();
    int secondMatchPos = aliceChatViewForSearch->textCursor().selectionStart();
    check(secondMatch.compare("hola", Qt::CaseInsensitive) == 0,
         "'siguiente' debe saltar a la segunda coincidencia de 'hola'");
    check(secondMatchPos != firstMatchPos,
         "'siguiente' debe moverse a una posicion distinta a la primera coincidencia");

    find<QPushButton>(&alice, "searchCloseButton")->click();
    check(!find<QWidget>(&alice, "searchBarWidget")->isVisible(),
         "la barra de busqueda debe ocultarse al cerrarla");
    check(searchEdit->text().isEmpty(), "el campo de busqueda debe vaciarse al cerrar");

    std::cout << "[OK] Busqueda dentro de la conversacion: cuenta resultados y navega entre "
                "coincidencias.\n";

    // --- Presencia en tiempo real: bob esta conectado, el punto debe estar verde ---
    QListWidgetItem* bobEntryInAlice = findConversationItem(aliceList, "gui_bob");
    check(bobEntryInAlice != nullptr, "gui_bob debe seguir en la barra lateral de alice");
    check(presenceDotColor(bobEntryInAlice) == QColor("#2ecc71"),
         "el punto de bob en la barra de alice debe estar verde mientras esta conectado");

    // --- Bob se desconecta -- alice debe verlo en gris SIN hacer nada ---
    find<QPushButton>(&bob, "disconnectButton")->click();
    QTest::qWait(500);

    check(presenceDotColor(bobEntryInAlice) == QColor("#777777"),
         "el punto de bob debe ponerse gris al desconectarse, en tiempo real");

    std::cout << "[OK] Presencia en tiempo real: verde mientras conectado, gris al desconectar.\n";
    std::cout << "[OK] Flujo completo de GUI real verificado.\n";
  } catch (const std::exception& e) {
    std::cerr << "[FAIL] " << e.what() << "\n";
    return 1;
  }

  return 0;
}
