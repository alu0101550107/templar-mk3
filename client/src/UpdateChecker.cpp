#include "UpdateChecker.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>

#ifndef TEMPLAR_APP_VERSION
#define TEMPLAR_APP_VERSION "0.0.0"
#endif

namespace templar {

namespace {

constexpr const char* kRepoOwner = "alu0101550107";
constexpr const char* kRepoName = "templar-mk3";
constexpr int kCheckIntervalHours = 24;

// Compara dos versiones "x.y.z" (con o sin "v" delante, asi el tag_name de
// GitHub -- tipicamente "v1.2.0" -- se compara tal cual contra
// TEMPLAR_APP_VERSION sin necesitar normalizar el signo de "v" a mano en
// cada sitio). Compara numero a numero (no como texto): "1.9.0" < "1.10.0",
// que una comparacion de strings dejaria al reves.
bool isNewerVersion(const QString& candidate, const QString& current) {
  auto parse = [](QString v) {
    if (v.startsWith('v') || v.startsWith('V')) v = v.mid(1);
    QStringList parts = v.split('.');
    QVector<int> nums;
    for (const QString& p : parts) nums.push_back(p.toInt());
    while (nums.size() < 3) nums.push_back(0);
    return nums;
  };

  QVector<int> a = parse(candidate);
  QVector<int> b = parse(current);
  for (int i = 0; i < 3; ++i) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

QString UpdateChecker::currentVersion() const { return QStringLiteral(TEMPLAR_APP_VERSION); }

QString UpdateChecker::releaseUrl() const {
  return QStringLiteral("https://github.com/%1/%2/releases/latest")
      .arg(QString::fromUtf8(kRepoOwner), QString::fromUtf8(kRepoName));
}

void UpdateChecker::checkIfDue() {
  QSettings settings;
  qint64 lastCheck = settings.value("lastUpdateCheckEpoch", 0).toLongLong();
  qint64 now = QDateTime::currentSecsSinceEpoch();
  if (now - lastCheck < static_cast<qint64>(kCheckIntervalHours) * 3600) return;

  settings.setValue("lastUpdateCheckEpoch", now);
  startCheck();
}

void UpdateChecker::startCheck() {
  QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                                   .arg(QString::fromUtf8(kRepoOwner), QString::fromUtf8(kRepoName))));
  // La API de GitHub responde 403 sin esto -- exige un User-Agent, no vale
  // dejarlo vacio (el que pone Qt por defecto).
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("templar-mk3-update-check"));

  QNetworkReply* reply = manager_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    // Un fallo aqui (sin red, GitHub caido, repo aun privado...) se ignora
    // en silencio a proposito -- comprobar actualizaciones nunca debe
    // molestar ni bloquear nada si no sale bien.
    if (reply->error() != QNetworkReply::NoError) return;

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) return;
    QString tagName = doc.object().value("tag_name").toString();
    if (tagName.isEmpty()) return;

    if (isNewerVersion(tagName, currentVersion())) {
      latestVersion_ = tagName;
      updateAvailable_ = true;
      emit updateAvailableChanged();
    }
  });
}

}  // namespace templar
