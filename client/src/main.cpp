#include <QApplication>
#include <QGuiApplication>
#include <QIcon>

#include "MainWindow.hpp"
#include "templar/crypto/Identity.hpp"

int main(int argc, char* argv[]) {
  templar::crypto::initSodium();

  QApplication app(argc, argv);
  QApplication::setOrganizationName("Templar");
  QApplication::setApplicationName("templar_client");
  // Icono de la propia ventana/app -- sin esto, el gestor de ventanas usa un
  // icono generico (el interrogante) en la barra de tareas y el alt-tab, aun
  // cuando la bandeja del sistema ya muestre el icono correcto.
  QApplication::setWindowIcon(QIcon(":/great_helmet_logo.png"));
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
