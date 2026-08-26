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
  Q_PROPERTY(QString apkDownloadUrl READ apkDownloadUrl CONSTANT)

 public:
  explicit UpdateChecker(QObject* parent = nullptr);

  // Se llama una vez al arrancar la app -- no hace ninguna peticion de red
  // si ya se comprobo hace menos de 24h (ver kCheckInterval en el .cpp).
  Q_INVOKABLE void checkIfDue();

  bool updateAvailable() const { return updateAvailable_; }
  QString latestVersion() const { return latestVersion_; }
  // Pagina de releases del proyecto -- SIEMPRE la misma URL ("latest",
  // GitHub redirige sola al release mas reciente). Para el cliente de
  // escritorio (que no se distribuye como un binario suelto, se compila
  // con setup.sh) es el enlace a usar siempre: "descargar la version
  // nueva" ahi significa git pull + recompilar, no bajar un fichero.
  QString releaseUrl() const;
  // Descarga DIRECTA del .apk del ultimo release -- sin pasar por la
  // pagina del release (que siempre trae ademas el zip/tar.gz del codigo
  // fuente que GitHub genera solo, facil de confundir con la app en si).
  // Usa el mismo truco de "latest" que releaseUrl(), pero con el nombre
  // fijo del asset (ver kApkAssetName en el .cpp) -- funciona sea cual sea
  // el ultimo release, siempre que cada release suba el apk con ESE mismo
  // nombre exacto. Solo tiene sentido en el cliente movil.
  QString apkDownloadUrl() const;
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
