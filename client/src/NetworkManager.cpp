#include "NetworkManager.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace templar::client {

using namespace templar::proto;

NetworkManager::NetworkManager(QObject* parent) : QObject(parent) {
  connect(&socket_, &QSslSocket::encrypted, this, &NetworkManager::onEncrypted);
  connect(&socket_, &QSslSocket::sslErrors, this, &NetworkManager::onSslErrors);
  connect(&socket_, &QSslSocket::disconnected, this, &NetworkManager::disconnected);
  connect(&socket_, &QSslSocket::readyRead, this, &NetworkManager::onReadyRead);
  connect(&socket_, &QAbstractSocket::errorOccurred, this, &NetworkManager::onSocketError);
}

void NetworkManager::connectToServer(const QString& host, quint16 port) {
  pendingHost_ = host;
  pendingPort_ = port;
  socket_.connectToHostEncrypted(host, port);
}

void NetworkManager::disconnectFromServer() { socket_.disconnectFromHost(); }

void NetworkManager::sendFrame(MsgType type, const Bytes& payload) {
  Bytes frame = encodeFrame(type, payload);
  socket_.write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));
}

void NetworkManager::onSslErrors(const QList<QSslError>& errors) {
  Q_UNUSED(errors);
  QSslCertificate cert = socket_.peerCertificate();
  // Autofirmado (sin ninguna CA detras) -- el caso de un servidor casero
  // sin dominio/Let's Encrypt. Aqui SI se ignoran los errores (nunca va a
  // haber una CA publica detras a proposito): la confianza real para este
  // caso se decide aparte, por huella, en onEncrypted() (TOFU). Un
  // certificado que SI viene de una CA reconocida (p.ej. Let's Encrypt)
  // pero tiene algun otro problema real (caducado, dominio que no
  // coincide, revocado...) ya NO se ignora -- antes esto se colaba
  // tambien sin querer al ignorar TODO incondicionalmente.
  if (cert.isNull() || !cert.isSelfSigned()) return;
  socket_.ignoreSslErrors();
}

void NetworkManager::onEncrypted() {
  QSslCertificate cert = socket_.peerCertificate();
  if (cert.isNull()) {
    emit connectionError("El servidor no presento ningun certificado TLS.");
    socket_.abort();
    return;
  }

  if (!checkAndRememberCertificate(pendingHost_, pendingPort_, cert)) {
    emit connectionError(
        QString("¡ALERTA! El certificado TLS de %1:%2 ha cambiado desde la ultima vez que te "
               "conectaste ahi. Podria ser un ataque de intermediario, o simplemente que "
               "reinstalaron el servidor -- si es lo segundo y confias en el cambio, borra la "
               "linea correspondiente en el archivo de hosts conocidos e intenta de nuevo. NO "
               "se ha enviado ningun dato.")
            .arg(pendingHost_)
            .arg(pendingPort_));
    socket_.abort();
    return;
  }

  emit connected();
}

QString NetworkManager::knownHostsFilePath() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/known_hosts.txt";
}

bool NetworkManager::checkAndRememberCertificate(const QString& host, quint16 port,
                                                 const QSslCertificate& cert) {
  const QString key = host + ":" + QString::number(port);
  const QString fingerprint = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());

  QString path = knownHostsFilePath();
  QStringList lines;
  QString existingFingerprint;

  QFile readFile(path);
  if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&readFile);
    while (!in.atEnd()) {
      QString line = in.readLine();
      QStringList parts = line.split(' ');
      if (parts.size() != 2) continue;
      lines << line;
      if (parts[0] == key) existingFingerprint = parts[1];
    }
    readFile.close();
  }

  if (!existingFingerprint.isEmpty()) {
    // Huella distinta a la que ya teniamos guardada para este host:puerto
    // -- NO se sobreescribe (si se sobreescribiera, un atacante en medio
    // solo tendria que colarse una vez para que la huella mala quedara
    // como la "de confianza" para siempre).
    return existingFingerprint == fingerprint;
  }

  // Primera vez que se habla con este host:puerto -- se confia y se
  // recuerda, igual que known_hosts de SSH en su primer uso.
  lines << (key + " " + fingerprint);
  QFile writeFile(path);
  if (writeFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QTextStream out(&writeFile);
    for (const QString& line : lines) out << line << "\n";
    writeFile.close();
  }
  return true;
}

void NetworkManager::onReadyRead() {
  QByteArray chunk = socket_.readAll();
  parser_.feed(reinterpret_cast<const uint8_t*>(chunk.constData()),
              static_cast<size_t>(chunk.size()));

  try {
    while (auto frame = parser_.next()) {
      emit frameReceived(frame->type, frame->payload);
    }
  } catch (const std::exception& e) {
    emit connectionError(QString::fromStdString(e.what()));
    socket_.disconnectFromHost();
  }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError) {
  emit connectionError(socket_.errorString());
}

}  // namespace templar::client
