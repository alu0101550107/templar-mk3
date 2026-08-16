#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "ClientController.hpp"
#include "ThemeController.hpp"
#include "templar/crypto/Identity.hpp"

int main(int argc, char* argv[]) {
  templar::crypto::initSodium();

  // Sin fijar esto, Qt elige un estilo de Qt Quick Controls por plataforma
  // (Fusion/Basic en escritorio, Material en Android) -- Material anima el
  // placeholder como una "etiqueta flotante" que sube al escribir, algo que
  // nuestros TemplarXxx (pensados para el estilo Basic, con fondo pintado a
  // mano) no esperan. Fijar "Basic" en todas las plataformas hace que el
  // movil se comporte igual que el escritorio.
  QQuickStyle::setStyle("Basic");

  QGuiApplication app(argc, argv);
  QGuiApplication::setOrganizationName("Templar");
  QGuiApplication::setApplicationName("templar_phone");

  templar::phone::ClientController controller;
  templar::phone::ThemeController theme;

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("controller", &controller);
  engine.rootContext()->setContextProperty("theme", &theme);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("Templar", "Main");

  return app.exec();
}
