#include <QApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QTranslator>

#include "Language.hpp"
#include "MainWindow.hpp"
#include "templar/crypto/Identity.hpp"

int main(int argc, char* argv[]) {
  templar::crypto::initSodium();

  QApplication app(argc, argv);
  QApplication::setOrganizationName("Templar");
  QApplication::setApplicationName("templar_client");

  // El codigo fuente (tr()) esta escrito en espanol, asi que ese idioma no
  // necesita ningun traductor instalado -- solo se carga uno para otro
  // idioma distinto (ver Language.hpp). El objeto vive en el scope de main()
  // para que no se destruya mientras dure app.exec().
  QTranslator translator;
  templar::Language language = templar::currentLanguage();
  if (language != templar::Language::Spanish &&
      translator.load(QStringLiteral(":/i18n/templar_%1.qm").arg(templar::languageCode(language)))) {
    QApplication::installTranslator(&translator);
  }
  // Icono de la propia ventana/app -- sin esto, el gestor de ventanas usa un
  // icono generico (el interrogante) en la barra de tareas y el alt-tab, aun
  // cuando la bandeja del sistema ya muestre el icono correcto.
  QApplication::setWindowIcon(QIcon(":/great_helmet_logo.png"));
  // Fuente de iconos propia (ver client/resources/icons/NOTICE.md) --
  // se carga una vez aqui para que su familia ("Templar Icons") este
  // disponible en toda la app sin depender de que el sistema tenga
  // instalados los codepoints de simbolos que usamos.
  QFontDatabase::addApplicationFont(":/templar-icons.ttf");
  // En Wayland (GNOME, Hyprland, etc.) la barra de tareas identifica la
  // ventana por su app_id y busca el icono en el archivo .desktop
  // correspondiente -- no en lo que se configura en tiempo de ejecucion.
  // Este nombre debe coincidir con el nombre de archivo del .desktop
  // instalado (ver templar-client.desktop) para que el icono se resuelva
  // correctamente.
  QGuiApplication::setDesktopFileName("templar-client");
  // El cliente se queda vivo en la bandeja del sistema al cerrar la ventana
  // (ver MainWindow::closeEvent) -- sin esto, Qt terminaria la app en
  // cuanto se oculta la unica ventana visible.
  QApplication::setQuitOnLastWindowClosed(false);

  templar::client::MainWindow window;
  window.show();
  return app.exec();
}
