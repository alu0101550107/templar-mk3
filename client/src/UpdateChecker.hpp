#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace templar {

// Comprueba en segundo plano si hay una version mas nueva publicada como
// GitHub Release, y expone un enlace a la pagina de releases -- no hay
// auto-actualizacion, solo un aviso discreto (ver MainWindow/ChatListPage.qml)
// mas un enlace fijo en Ajustes que funciona haya o no version nueva. Como
// mucho una vez al dia (persistido en QSettings), para no golpear la API de
// GitHub en cada arranque ni comportarse como una app que "llama a casa"
// constantemente -- coherente con el resto del proyecto (nada de telemetria).
class UpdateChecker : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateAvailableChanged)
  Q_PROPERTY(QString releaseUrl READ releaseUrl CONSTANT)

 public:
  explicit UpdateChecker(QObject* parent = nullptr);

  // Se llama una vez al arrancar la app -- no hace ninguna peticion de red
  // si ya se comprobo hace menos de 24h (ver kCheckInterval en el .cpp).
  Q_INVOKABLE void checkIfDue();

  bool updateAvailable() const { return updateAvailable_; }
  QString latestVersion() const { return latestVersion_; }
  // Pagina de releases del proyecto -- SIEMPRE la misma URL ("latest",
  // GitHub redirige sola al release mas reciente), asi que sirve tanto de
  // enlace fijo en Ajustes (independiente de si hay o no version nueva)
  // como de destino del aviso cuando si la hay.
  QString releaseUrl() const;
  // Version compilada en este binario -- ver CMakeLists.txt (project(...
  // VERSION x.y.z)) y TEMPLAR_APP_VERSION en el .cpp.
  QString currentVersion() const;

 signals:
  void updateAvailableChanged();

 private:
  void startCheck();

  QNetworkAccessManager* manager_;
  bool updateAvailable_ = false;
  QString latestVersion_;
};

}  // namespace templar
