#pragma once

#include <QObject>
#include <QSslCertificate>
#include <QSslSocket>

#include "templar/Protocol.hpp"

namespace templar::client {

// Envuelve QSslSocket con el framing TLV de templar::proto. La conexion
// siempre va cifrada con TLS -- el certificado del servidor es autofirmado
// (no hay CA publica para un servidor casero), asi que la confianza se
// decide por TOFU (trust-on-first-use, el mismo criterio que known_hosts de
// SSH): la primera vez que se habla con un host:puerto se acepta y se
// recuerda su huella; si en una conexion futura el certificado cambiase sin
// avisar, se rechaza y se avisa fuerte (ver checkAndRememberCertificate).
class NetworkManager : public QObject {
  Q_OBJECT

 public:
  explicit NetworkManager(QObject* parent = nullptr);

  void connectToServer(const QString& host, quint16 port);
  void disconnectFromServer();
  void sendFrame(templar::proto::MsgType type, const templar::proto::Bytes& payload);

 signals:
  // Solo se emite tras completarse el handshake TLS Y pasar la comprobacion
  // TOFU -- a diferencia de QTcpSocket::connected, que solo confirma que el
  // TCP crudo conecto (sin ninguna garantia de que sea de verdad el servidor
  // esperado).
  void connected();
  void disconnected();
  void connectionError(QString message);
  void frameReceived(templar::proto::MsgType type, templar::proto::Bytes payload);

 private slots:
  void onEncrypted();
  void onSslErrors(const QList<QSslError>& errors);
  void onReadyRead();
  void onSocketError(QAbstractSocket::SocketError error);

 private:
  bool checkAndRememberCertificate(const QString& host, quint16 port, const QSslCertificate& cert);
  static QString knownHostsFilePath();

  QSslSocket socket_;
  templar::proto::FrameParser parser_;
  QString pendingHost_;
  quint16 pendingPort_ = 0;
};

}  // namespace templar::client
