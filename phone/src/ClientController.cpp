#include "ClientController.hpp"

#include <sodium.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include "templar/Wire.hpp"
#include "templar/crypto/Identity.hpp"

namespace templar::phone {

using namespace templar::proto;
using templar::client::DecodedPayload;
using templar::client::LocalStore;
using templar::client::MessagePayload;
using templar::client::PayloadKind;
using templar::client::PersistedIdentity;
using templar::client::UnlockResult;

namespace {
// Mismos umbrales que kOneTimePrekeyLowWatermark/kOneTimePrekeyTarget en
// MainWindow.hpp del escritorio.
constexpr size_t kOneTimePrekeyLowWatermark = 5;
constexpr size_t kOneTimePrekeyTarget = 20;

// Mismos limites que kMaxFileSize/kFileChunkSize en MainWindow.hpp del
// escritorio -- ver el comentario de OutgoingTransfer en el .hpp.
constexpr quint64 kMaxFileSize = 200ull * 1024 * 1024;
constexpr qint64 kFileChunkSize = 64 * 1024;

// Mismo truco que onFileMetaReceived en el escritorio: si el nombre ya
// existe en el destino, se le anade " (1)", " (2)", etc. antes de la
// extension en vez de sobrescribir.
QString uniqueDownloadPath(const QString& dir, const QString& filename) {
  QString candidate = dir + "/" + filename;
  if (!QFile::exists(candidate)) return candidate;

  QFileInfo info(filename);
  QString base = info.completeBaseName();
  QString ext = info.suffix();
  for (int i = 1;; ++i) {
    QString altName = ext.isEmpty() ? QString("%1 (%2)").arg(base).arg(i)
                                    : QString("%1 (%2).%3").arg(base).arg(i).arg(ext);
    QString altPath = dir + "/" + altName;
    if (!QFile::exists(altPath)) return altPath;
  }
}

// Decide si un archivo descargado va a la coleccion de imagenes de
// MediaStore (Galeria) o a la de Descargas -- ver openMediaStoreFd. Basta
// con la extension: no hace falta abrir el archivo ni fiarse de nada que
// mande el otro lado mas alla de eso.
bool isImageFilename(const QString& filename) {
  const QString ext = QFileInfo(filename).suffix().toLower();
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp" ||
         ext == "bmp" || ext == "heic" || ext == "heif";
}
}  // namespace

ClientController* ClientController::instance_ = nullptr;

ClientController::ClientController(QObject* parent) : QObject(parent) {
  instance_ = this;

  // Se registra ya desde el arranque (no hace falta esperar al login) --
  // igual que "Sistema" en el escritorio, para que los mensajes de antes
  // de iniciar sesion (p.ej. errores de conexion) tengan donde aparecer.
  conversations_.upsert(QString::fromLatin1(kSystemKey), tr("Sistema"), false);
  // "Sistema" no es un peer de verdad, no tiene un estado online/offline
  // que mostrar -- a diferencia del escritorio (que directamente no le
  // pone icono, ver MainWindow::ensureConversationListed), aqui el punto
  // de presencia se dibuja para toda entrada que no sea grupo (ver
  // ChatListPage.qml), asi que lo mas simple es dejarlo fijo "en linea"
  // en vez de tocar tambien el QML para ocultarlo.
  conversations_.setOnline(QString::fromLatin1(kSystemKey), true);

  connect(&net_, &templar::client::NetworkManager::connected, this,
         &ClientController::onNetConnected);
  connect(&net_, &templar::client::NetworkManager::disconnected, this,
         &ClientController::onNetDisconnected);
  connect(&net_, &templar::client::NetworkManager::connectionError, this,
         &ClientController::onNetError);
  connect(&net_, &templar::client::NetworkManager::frameReceived, this,
         &ClientController::onFrameReceived);

  fileSendTimer_ = new QTimer(this);
  connect(fileSendTimer_, &QTimer::timeout, this, &ClientController::sendNextFileChunk);

  setupAndroidNotifications();
}

ClientController::~ClientController() {
  if (instance_ == this) instance_ = nullptr;

  // net_ es el PRIMER miembro declarado en el .hpp, asi que se destruye EL
  // ULTIMO (los miembros se destruyen en orden inverso de declaracion) --
  // destruir un QSslSocket todavia conectado dispara disconnectFromHost()
  // dentro de su propio destructor, que emite `disconnected` de forma
  // SINCRONA. Sin este disconnect() explicito, esa senal reenganchaba
  // onNetDisconnected(), que tocaba conversations_/conversationHistory_ --
  // miembros declarados DESPUES de net_, y por tanto ya destruidos en ese
  // punto. Use-after-free real, confirmado por Android (Scudo: "invalid
  // chunk state when deallocating") al cerrar la app estando conectada.
  net_.disconnect();
}

void ClientController::setupAndroidNotifications() {
#ifdef Q_OS_ANDROID
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  QJniObject::callStaticMethod<void>("com/templar/phone/NotificationHelper", "ensureChannel",
                                     "(Landroid/content/Context;)V", activity.object());
  QJniObject::callStaticMethod<void>("com/templar/phone/NotificationHelper", "requestPermission",
                                     "(Landroid/app/Activity;)V", activity.object());

  QJniObject app = activity.callObjectMethod("getApplication", "()Landroid/app/Application;");
  if (app.isValid()) {
    QJniObject::callStaticMethod<void>("com/templar/phone/AppStateTracker", "register",
                                       "(Landroid/app/Application;)V", app.object());
  }

  QJniEnvironment jniEnv;
  jniEnv.registerNativeMethods(
      "com/templar/phone/CameraHelper",
      {{"nativeOnPhotoCaptured", "(Ljava/lang/String;Z)V",
        reinterpret_cast<void*>(&ClientController::onPhotoCapturedJni)}});
#endif
}

#ifdef Q_OS_ANDROID
void ClientController::onPhotoCapturedJni(JNIEnv* env, jclass, jstring path, jboolean success) {
  Q_UNUSED(env);
  ClientController* self = instance_;
  if (!self) return;
  QString qpath = path ? QJniObject(path).toString() : QString();
  bool ok = success != 0;
  // Este callback lo invoca Android en SU propio hilo (el hilo de UI de
  // Java), no en el hilo de Qt donde vive ClientController/fileSendTimer_
  // -- llamar a QTimer::start() (dentro de sendFile(), via
  // onPhotoCaptureFinished) desde el hilo equivocado no funciona bien
  // (Qt exige que un QTimer se arranque desde su propio hilo). invokeMethod
  // con Qt::QueuedConnection reenvia la llamada al hilo correcto.
  QMetaObject::invokeMethod(
      self, [self, qpath, ok]() { self->onPhotoCaptureFinished(qpath, ok); },
      Qt::QueuedConnection);
}
#endif

void ClientController::capturePhoto(const QString& peerKey) {
#ifdef Q_OS_ANDROID
  // Mismas condiciones que sendFile() (ya no hace falta que el destinatario
  // este en linea, los grupos si estan soportados, y el chat contigo mismo
  // tampoco necesita sesion -- ver el comentario de startSelfFileAttach) --
  // se comprueban aqui tambien para no molestar al usuario con la camara
  // del sistema si el envio va a fallar de todos modos.
  bool isSelf = peerKey.toStdString() == myUsername_;
  if (!isSelf && !conversations_.isGroupChat(peerKey) &&
      !crypto_.hasSession(peerKey.toStdString())) {
    setErrorText(tr("Manda primero un mensaje de texto a %1 para establecer la conversacion "
                    "antes de enviar archivos.")
                     .arg(peerKey));
    return;
  }
  if (outgoingTransfer_ || activeDownload_) {
    setErrorText(tr("Ya hay una transferencia de archivo en curso, espera a que termine."));
    return;
  }

  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  pendingPhotoPeerKey_ = peerKey;

  // "<paquete>.qtprovider" -- misma autoridad que declara el <provider>
  // FileProvider en AndroidManifest.xml (android:authorities="${applicationId}.qtprovider").
  QJniObject packageName = activity.callObjectMethod("getPackageName", "()Ljava/lang/String;");
  QString authorityStr = packageName.toString() + ".qtprovider";
  QJniObject jAuthority = QJniObject::fromString(authorityStr);

  QJniObject::callStaticMethod<void>(
      "com/templar/phone/CameraHelper", "capturePhoto",
      "(Landroid/app/Activity;Ljava/lang/String;)V", activity.object(), jAuthority.object<jstring>());
#else
  Q_UNUSED(peerKey);
#endif
}

void ClientController::onPhotoCaptureFinished(const QString& path, bool success) {
  QString peerKey = pendingPhotoPeerKey_;
  pendingPhotoPeerKey_.clear();
  if (!success || path.isEmpty() || peerKey.isEmpty()) return;
  sendFile(peerKey, QUrl::fromLocalFile(path));
}

#ifdef Q_OS_ANDROID
int ClientController::openMediaStoreFd(bool isImage, const QString& displayName) {
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return -1;
  QJniObject jName = QJniObject::fromString(displayName);
  const char* method = isImage ? "openImageOutputFd" : "openDownloadOutputFd";
  return QJniObject::callStaticMethod<jint>("com/templar/phone/MediaStoreHelper", method,
                                            "(Landroid/content/Context;Ljava/lang/String;)I",
                                            activity.object(), jName.object<jstring>());
}

void ClientController::finalizeMediaStoreWrite(bool keep) {
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  QJniObject::callStaticMethod<void>("com/templar/phone/MediaStoreHelper", "finishPending",
                                     "(Landroid/content/Context;Z)V", activity.object(),
                                     static_cast<jboolean>(keep));
}

QString ClientController::queryContentDisplayName(const QUrl& fileUrl) {
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return QString();
  QJniObject jUri = QJniObject::fromString(fileUrl.toString());
  QJniObject result = QJniObject::callStaticObjectMethod(
      "com/templar/phone/ContentUriHelper", "queryDisplayName",
      "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", activity.object(),
      jUri.object<jstring>());
  return result.isValid() ? result.toString() : QString();
}
#endif

QString ClientController::resolveOriginalFilename(const QUrl& fileUrl) {
  QString name;
#ifdef Q_OS_ANDROID
  if (fileUrl.scheme() == "content") name = queryContentDisplayName(fileUrl);
#endif
  if (name.isEmpty()) name = fileUrl.fileName();
  return QFileInfo(name).fileName();
}

bool ClientController::shouldNotify(const QString& conversationKey) const {
  // Desactivado a proposito por ahora: incluso con la comprobacion de mas
  // abajo (no avisar si la conversacion que llega es la que ya esta
  // abierta en primer plano), seguian llegando avisos de sistema estando
  // dentro de ese mismo chat -- probablemente AppStateTracker.isInForeground()
  // no es fiable en este dispositivo/version de Android. El resto de la
  // infraestructura (canal, permiso, JNI, ConnectionService) se deja tal
  // cual para retomarlo mas adelante; solo se apaga la decision de
  // mostrar el aviso, con este flag.
  constexpr bool kSystemNotificationsEnabled = false;
  if (!kSystemNotificationsEnabled) return false;

#ifdef Q_OS_ANDROID
  // QGuiApplication::applicationState() de Qt no cambia de forma fiable en
  // Android con ConnectionService corriendo (el proceso sigue "activo"
  // aunque la Activity este pausada) -- se le pregunta a Android
  // directamente en su lugar, via AppStateTracker.
  bool inForeground =
      QJniObject::callStaticMethod<jboolean>("com/templar/phone/AppStateTracker", "isInForeground");
#else
  bool inForeground = qGuiApp->applicationState() == Qt::ApplicationActive;
#endif
  bool viewingThisConversation = inForeground && conversationKey == activeConversationKey_;
  return !viewingThisConversation;
}

void ClientController::startBackgroundConnectionService() {
#ifdef Q_OS_ANDROID
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  QJniObject::callStaticMethod<void>("com/templar/phone/ConnectionService", "start",
                                     "(Landroid/content/Context;)V", activity.object());
#endif
}

void ClientController::stopBackgroundConnectionService() {
#ifdef Q_OS_ANDROID
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  QJniObject::callStaticMethod<void>("com/templar/phone/ConnectionService", "stop",
                                     "(Landroid/content/Context;)V", activity.object());
#endif
}

void ClientController::showSystemNotification(const QString& title, const QString& text) {
#ifdef Q_OS_ANDROID
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;
  QJniObject jTitle = QJniObject::fromString(title);
  QJniObject jText = QJniObject::fromString(text);
  QJniObject::callStaticMethod<void>(
      "com/templar/phone/NotificationHelper", "show",
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V", activity.object(),
      jTitle.object<jstring>(), jText.object<jstring>());
#else
  Q_UNUSED(title);
  Q_UNUSED(text);
#endif
}

void ClientController::setConnected(bool value) {
  if (connected_ == value) return;
  connected_ = value;
  emit connectedChanged();
}

void ClientController::setLoggedIn(bool value) {
  if (loggedIn_ == value) return;
  loggedIn_ = value;
  emit loggedInChanged();
}

void ClientController::setStatusText(const QString& text) {
  if (statusText_ == text) return;
  statusText_ = text;
  emit statusTextChanged();
}

void ClientController::setErrorText(const QString& text) {
  if (errorText_ == text) return;
  errorText_ = text;
  emit errorTextChanged();
}

void ClientController::setUsername(const std::string& value) {
  if (myUsername_ == value) return;
  myUsername_ = value;
  emit usernameChanged();
}

void ClientController::setServerAddress(const QString& value) {
  if (serverAddress_ == value) return;
  serverAddress_ = value;
  emit serverAddressChanged();
}

void ClientController::setFileTransferStatus(const QString& status) {
  if (fileTransferStatus_ == status) return;
  fileTransferStatus_ = status;
  emit fileTransferStatusChanged();
}

void ClientController::setFileTransferProgress(double value) {
  if (fileTransferProgress_ == value) return;
  fileTransferProgress_ = value;
  emit fileTransferProgressChanged();
}

void ClientController::connectToServer(const QString& hostAndPort) {
  setErrorText("");

  QString host = hostAndPort;
  quint16 port = 8080;
  int colonIdx = hostAndPort.lastIndexOf(':');
  if (colonIdx > 0) {
    bool ok = false;
    uint parsedPort = hostAndPort.mid(colonIdx + 1).toUInt(&ok);
    if (ok && parsedPort > 0 && parsedPort <= 65535) {
      host = hostAndPort.left(colonIdx);
      port = static_cast<quint16>(parsedPort);
    }
  }

  setServerAddress(host + ":" + QString::number(port));
  setStatusText(tr("Conectando..."));
  net_.connectToServer(host, port);

  QSettings settings;
  settings.setValue("lastServerAddress", serverAddress_);
}

QString ClientController::rememberedServerAddress() const {
  return QSettings().value("lastServerAddress").toString();
}

QString ClientController::rememberedUsername() const {
  return QSettings().value("lastUsername").toString();
}

void ClientController::disconnectFromServer() { net_.disconnectFromServer(); }

void ClientController::subscribePresence(const QString& peerUsername) {
  Writer w;
  w.str(peerUsername.toStdString());
  net_.sendFrame(MsgType::SubscribePresence, w.take());
}

void ClientController::startChat(const QString& peerUsername) {
  QString trimmed = peerUsername.trimmed();
  if (trimmed.isEmpty()) return;
  bool isNew = conversations_.upsert(trimmed, trimmed, /*isGroup=*/false);
  if (isNew) subscribePresence(trimmed);
}

void ClientController::createGroup(const QString& name, const QStringList& inviteUsernames) {
  QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return;
  pendingGroupInvitees_ = inviteUsernames;
  Writer w;
  w.str(trimmed.toStdString());
  net_.sendFrame(MsgType::CreateGroup, w.take());
}

void ClientController::inviteToGroup(const QString& groupKey, const QString& username) {
  QString trimmed = username.trimmed();
  if (trimmed.isEmpty()) return;
  Writer w;
  w.str(groupKey.toStdString());
  w.str(trimmed.toStdString());
  net_.sendFrame(MsgType::InviteToGroup, w.take());
}

void ClientController::kickFromGroup(const QString& groupKey, const QString& username) {
  if (username.isEmpty()) return;
  Writer w;
  w.str(groupKey.toStdString());
  w.str(username.toStdString());
  net_.sendFrame(MsgType::KickFromGroup, w.take());
}

void ClientController::leaveGroup(const QString& groupKey) {
  std::string groupId = groupKey.toStdString();
  if (groupId.empty()) return;

  Writer w;
  w.str(groupId);
  net_.sendFrame(MsgType::LeaveGroup, w.take());

  // Salir es una accion propia -- se refleja ya en la UI sin esperar
  // confirmacion del servidor (a diferencia de una expulsion, que la
  // decide otra persona y por eso si hay que esperar su notificacion, ver
  // GroupMemberKicked).
  conversations_.removeEntry(groupKey);
  if (activeConversationKey_ == groupKey) {
    activeConversationKey_.clear();
    history_.setLines({});
  }
}

QString ClientController::groupAdmin(const QString& groupKey) const {
  return conversations_.adminOf(groupKey);
}

QStringList ClientController::groupMembers(const QString& groupKey) const {
  return conversations_.membersOf(groupKey);
}

void ClientController::respondToGroupInvite(quint32 inviteId, bool accept) {
  Writer w;
  w.u32(inviteId);
  net_.sendFrame(accept ? MsgType::AcceptGroupInvite : MsgType::RejectGroupInvite, w.take());

  // Se quita de la lista en caliente (optimista), igual que
  // respondToSelectedInvite en el escritorio -- no se espera confirmacion
  // del servidor para que desaparezca del panel.
  for (int i = 0; i < pendingInvites_.size(); ++i) {
    if (pendingInvites_.at(i).toMap().value("inviteId").toUInt() == inviteId) {
      pendingInvites_.removeAt(i);
      break;
    }
  }
  emit pendingInvitesChanged();
}

void ClientController::acceptGroupInvite(quint32 inviteId) { respondToGroupInvite(inviteId, true); }

void ClientController::rejectGroupInvite(quint32 inviteId) {
  respondToGroupInvite(inviteId, false);
}

void ClientController::setActiveConversation(const QString& key) {
  // No tiene sentido dejar una respuesta pendiente apuntando a un mensaje
  // de un chat distinto al que se esta a punto de escribir.
  cancelReply();
  activeConversationKey_ = key;
  auto it = conversationHistory_.find(key.toStdString());
  history_.setLines(it != conversationHistory_.end() ? it->second : std::vector<ChatLine>{});
  clearUnread(key.toStdString());
}

void ClientController::clearActiveConversation() {
  cancelReply();
  activeConversationKey_.clear();
  history_.setLines({});
}

void ClientController::markUnread(const std::string& key) {
  int newCount = ++unreadCounts_[key];
  conversations_.setUnreadCount(QString::fromStdString(key), newCount);
  if (localStore_.isUnlocked()) {
    try {
      localStore_.setUnreadCount(key, newCount);
    } catch (const std::exception&) {
    }
  }
}

void ClientController::clearUnread(const std::string& key) {
  auto it = unreadCounts_.find(key);
  if (it == unreadCounts_.end() || it->second == 0) return;
  it->second = 0;
  conversations_.setUnreadCount(QString::fromStdString(key), 0);
  if (localStore_.isUnlocked()) {
    try {
      localStore_.setUnreadCount(key, 0);
    } catch (const std::exception&) {
    }
  }
}

void ClientController::logChat(const std::string& peerKey, int kind, const QString& who,
                               const QString& text, bool rawHtml, const QString& persistText,
                               qint64 timestamp, const QString& replyToSender,
                               const QString& replyToText) {
  qint64 effectiveTimestamp = timestamp != 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
  ChatLine line{kind, who, text, effectiveTimestamp, rawHtml, replyToSender, replyToText};
  conversationHistory_[peerKey].push_back(line);
  if (activeConversationKey_.toStdString() == peerKey) history_.append(line);

  if (localStore_.isUnlocked()) {
    try {
      const QString& toPersist = persistText.isEmpty() ? text : persistText;
      localStore_.appendHistoryLine(peerKey, kind, who.toStdString(), toPersist.toStdString(),
                                    rawHtml, effectiveTimestamp, replyToSender.toStdString(),
                                    replyToText.toStdString());
    } catch (const std::exception&) {
      // Un fallo persistiendo no debe romper el envio/recepcion en vivo --
      // mismo criterio que MainWindow::logChat en el escritorio.
    }
  }
}

void ClientController::logSystem(const QString& text) {
  logChat(kSystemKey, /*System=*/0, QString(), text);
}

void ClientController::startReply(const QString& sender, const QString& text) {
  pendingReplyToSender_ = sender;
  pendingReplyToText_ = text;
  hasPendingReply_ = true;
  emit pendingReplyChanged();
}

void ClientController::cancelReply() {
  if (!hasPendingReply_) return;
  hasPendingReply_ = false;
  pendingReplyToSender_.clear();
  pendingReplyToText_.clear();
  emit pendingReplyChanged();
}

Bytes ClientController::encodeOutgoingText(const std::string& text) const {
  if (hasPendingReply_) {
    return MessagePayload::encodeTextReply(text, pendingReplyToSender_.toStdString(),
                                           pendingReplyToText_.toStdString());
  }
  return MessagePayload::encodeText(text);
}

void ClientController::sendEncryptedToServer(const std::string& peer, const Bytes& ciphertext) {
  Writer w;
  w.str(peer);
  w.u8(0);
  w.blob(Bytes());
  w.blob(Bytes());
  w.blob(ciphertext);
  w.blob(Bytes());  // usedOneTimePrekeyPub: solo aplica al primer mensaje
  net_.sendFrame(MsgType::SendMsg, w.take());
}

void ClientController::sendGroupCiphertextToServer(const std::string& groupId,
                                                    const std::string& recipient,
                                                    const Bytes& ciphertext) {
  Writer w;
  w.str(groupId);
  w.str(recipient);
  w.u8(0);
  w.blob(Bytes());
  w.blob(Bytes());
  w.blob(ciphertext);
  w.blob(Bytes());  // usedOneTimePrekeyPub: solo aplica al primer mensaje
  net_.sendFrame(MsgType::SendGroupMsg, w.take());
}

void ClientController::continueGroupFanout() {
  if (!activeGroupFanout_) return;
  GroupFanout& fanout = *activeGroupFanout_;

  while (!fanout.remainingMembers.empty()) {
    std::string member = fanout.remainingMembers.back();
    fanout.remainingMembers.pop_back();

    if (crypto_.hasSession(member)) {
      Bytes ciphertext = crypto_.encryptNext(member, fanout.payload);
      sendGroupCiphertextToServer(fanout.groupId, member, ciphertext);
      trySaveSession(member);
    } else {
      pendingOutboundPeer_ = member;
      pendingOutboundPayload_ = fanout.payload;
      pendingOutboundGroupId_ = fanout.groupId;
      pendingOutboundOwnLineText_.clear();
      pendingOutboundReplyToSender_.clear();
      pendingOutboundReplyToText_.clear();
      Writer w;
      w.str(member);
      net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
      return;
    }
  }

  activeGroupFanout_.reset();
}

void ClientController::trySaveSession(const std::string& peer) {
  if (!localStore_.isUnlocked()) return;
  try {
    localStore_.saveSession(peer, crypto_.exportSession(peer));
  } catch (const std::exception&) {
    // idem logChat: no debe romper el flujo en vivo.
  }
}

std::optional<templar::crypto::X25519KeyPair> ClientController::takeMatchingOneTimePrekey(
    const Bytes& usedOneTimePrekeyPub) {
  if (usedOneTimePrekeyPub.empty() || !localStore_.isUnlocked()) return std::nullopt;
  try {
    auto secret = localStore_.takeOneTimePrekeySecret(usedOneTimePrekeyPub);
    if (!secret || secret->size() != crypto_box_SECRETKEYBYTES ||
        usedOneTimePrekeyPub.size() != crypto_box_PUBLICKEYBYTES) {
      return std::nullopt;
    }
    templar::crypto::X25519KeyPair kp{};
    std::copy(usedOneTimePrekeyPub.begin(), usedOneTimePrekeyPub.end(), kp.pk);
    std::copy(secret->begin(), secret->end(), kp.sk);
    return kp;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void ClientController::ensureOneTimePrekeys() {
  if (!localStore_.isUnlocked()) return;
  try {
    if (localStore_.countOneTimePrekeys() < kOneTimePrekeyLowWatermark) {
      while (localStore_.countOneTimePrekeys() < kOneTimePrekeyTarget) {
        auto kp = templar::crypto::generateX25519KeyPair();
        Bytes pub(kp.pk, kp.pk + crypto_box_PUBLICKEYBYTES);
        Bytes sec(kp.sk, kp.sk + crypto_box_SECRETKEYBYTES);
        localStore_.saveOneTimePrekey(pub, sec);
      }
    }

    // Se republica el pool local completo, no solo lo nuevo -- el servidor
    // ignora silenciosamente las que ya tenia (misma clave publica).
    std::vector<Bytes> publics = localStore_.listOneTimePrekeyPublics();
    Writer w;
    w.u32(static_cast<uint32_t>(publics.size()));
    for (const Bytes& pub : publics) w.blob(pub);
    net_.sendFrame(MsgType::PublishOtpk, w.take());
  } catch (const std::exception&) {
    // idem: no debe impedir que el login cuente como exitoso.
  }
}

void ClientController::loadHistoryFromStore() {
  knownGroupKeys_.clear();
  for (const std::string& groupId : localStore_.loadKnownGroupKeys()) {
    knownGroupKeys_.insert(groupId);
  }

  for (const std::string& key : localStore_.listConversationKeys()) {
    // Un grupo (actual o viejo) nunca se lista aqui con la clave en crudo
    // como si fuera un usuario -- si todavia soy miembro, el MyGroups que
    // llega justo despues de LoginOk lo listara con su nombre real
    // (conversations_.upsert con isGroup=true); si ya no lo soy, no vuelve
    // a listarse en absoluto. Pero en AMBOS casos su historial se carga
    // igual, abajo -- antes se saltaba entero con este mismo "if" (bug
    // real: el historial de un grupo desaparecia al cerrar y reabrir la
    // app, aunque el grupo en si seguia ahi via MyGroups).
    // "Sistema" y el chat contigo mismo ya estan registrados con su
    // etiqueta correcta ("Sistema"/"Tú") desde antes de llamar a esta
    // funcion -- a diferencia de ensureConversationListed en el
    // escritorio (que no toca nada si la clave ya existe),
    // ConversationListModel::upsert SI actualiza el nombre cuando
    // difiere (hace falta para que un grupo renombrado se refresque), asi
    // que sin este "if" el bucle de aqui abajo pisaba esas dos etiquetas
    // con la clave en crudo (bug real: "Sistema" se quedaba en blanco
    // tras cerrar y reabrir sesion, en cuanto hubiera algun mensaje de
    // sistema persistido).
    if (key != kSystemKey && key != myUsername_ && !knownGroupKeys_.count(key)) {
      QString qkey = QString::fromStdString(key);
      bool isNew = conversations_.upsert(qkey, qkey, /*isGroup=*/false);
      if (isNew) subscribePresence(qkey);
    }

    std::vector<ChatLine> lines;
    for (const auto& hl : localStore_.loadHistory(key)) {
      lines.push_back(ChatLine{hl.kind, QString::fromStdString(hl.who),
                               QString::fromStdString(hl.text), hl.createdAt, hl.rawHtml,
                               QString::fromStdString(hl.replyToSender),
                               QString::fromStdString(hl.replyToText)});
    }
    conversationHistory_[key] = std::move(lines);
  }
}

void ClientController::sendMessage(const QString& peerKey, const QString& text) {
  std::string peer = peerKey.toStdString();
  std::string plaintext = text.toStdString();
  if (peer.empty() || plaintext.empty()) return;

  // Se capturan ANTES de mandar nada: cancelReply() (al final de esta
  // funcion) los borra, y el bootstrap X3DH (rama de abajo) los necesita
  // mas tarde, cuando ya se habran limpiado.
  QString replySender = pendingReplyToSender_;
  QString replyText = pendingReplyToText_;

  if (peer == myUsername_) {
    // Chat contigo mismo: una nota local, no un mensaje de verdad -- no
    // hay a quien mas llegarle, asi que no tiene sentido cifrar/mandar
    // nada por la red (ver el comentario de startSelfFileAttach).
    logChat(peer, /*Own=*/1, username(), text, /*rawHtml=*/false, /*persistText=*/QString(),
           /*timestamp=*/0, replySender, replyText);
    cancelReply();
    return;
  }

  if (crypto_.hasSession(peer)) {
    Bytes ciphertext = crypto_.encryptNext(peer, encodeOutgoingText(plaintext));
    sendEncryptedToServer(peer, ciphertext);
    logChat(peer, /*Own=*/1, username(), text, false, QString(), 0, replySender, replyText);
    trySaveSession(peer);
  } else {
    // Necesitamos el prekey bundle del peer antes de poder cifrar el
    // primer mensaje (X3DH) -- se completa en el caso PrekeyBundle de
    // onFrameReceived cuando llegue.
    pendingOutboundPeer_ = peer;
    pendingOutboundPayload_ = encodeOutgoingText(plaintext);
    pendingOutboundGroupId_.clear();
    pendingOutboundOwnLineText_ = text;
    pendingOutboundReplyToSender_ = replySender;
    pendingOutboundReplyToText_ = replyText;
    Writer w;
    w.str(peer);
    net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
  }
  cancelReply();
}

void ClientController::sendGroupMessage(const QString& groupKey, const QString& text) {
  std::string groupId = groupKey.toStdString();
  std::string plaintext = text.toStdString();
  if (groupId.empty() || plaintext.empty()) return;

  QStringList qMembers = conversations_.membersOf(groupKey);
  if (qMembers.isEmpty()) return;

  // Se registra la linea propia una sola vez, ya (optimista) -- igual que
  // al mandar a un peer con sesion ya establecida, no se espera a que cada
  // reparto individual confirme para mostrarla.
  logChat(groupId, /*Own=*/1, username(), text, /*rawHtml=*/false, /*persistText=*/QString(),
         /*timestamp=*/0, pendingReplyToSender_, pendingReplyToText_);

  std::vector<std::string> recipients;
  for (const QString& member : qMembers) {
    if (member.toStdString() != myUsername_) recipients.push_back(member.toStdString());
  }

  activeGroupFanout_ = GroupFanout{groupId, encodeOutgoingText(plaintext), std::move(recipients)};
  continueGroupFanout();
  cancelReply();
}

void ClientController::sendFile(const QString& peerKey, const QUrl& fileUrl) {
  std::string peer = peerKey.toStdString();
  if (peer.empty()) return;
  if (peer == myUsername_) {
    startSelfFileAttach(fileUrl);
    return;
  }
  bool isGroup = conversations_.isGroupChat(peerKey);
  if (!isGroup && !crypto_.hasSession(peer)) {
    setErrorText(tr("Manda primero un mensaje de texto a %1 para establecer la conversacion "
                    "antes de enviar archivos.")
                     .arg(peerKey));
    return;
  }
  if (outgoingTransfer_ || activeDownload_) {
    setErrorText(tr("Ya hay una transferencia de archivo en curso, espera a que termine."));
    return;
  }

  // QFile abre "content://..." directo en Android (el motor de archivos
  // propio de Qt para Android lo resuelve via el content resolver del
  // sistema) igual que una ruta normal en el resto de plataformas -- no
  // hace falta distinguir el esquema aqui.
  auto file = std::make_unique<QFile>(fileUrl.isLocalFile() ? fileUrl.toLocalFile()
                                                             : fileUrl.toString());
  qint64 size = file->size();
  if (size <= 0 || !file->open(QIODevice::ReadOnly)) {
    setErrorText(tr("No se pudo abrir el archivo seleccionado para leerlo."));
    return;
  }
  if (static_cast<quint64>(size) > kMaxFileSize) {
    setErrorText(
        tr("El archivo supera el limite de %1 MB de esta version.").arg(kMaxFileSize / (1024 * 1024)));
    return;
  }

  OutgoingTransfer transfer;
  transfer.peer = peer;
  transfer.isGroup = isGroup;
  transfer.filename = resolveOriginalFilename(fileUrl);
  if (transfer.filename.isEmpty()) transfer.filename = tr("archivo_enviado");
  transfer.totalSize = static_cast<quint64>(size);
  transfer.file = std::move(file);
  transfer.key = templar::crypto::generateFileKey();
  transfer.encryptor = std::make_unique<templar::crypto::FileEncryptor>(transfer.key);
  outgoingTransfer_ = std::move(transfer);

  Writer w;
  w.u64(outgoingTransfer_->totalSize);
  w.str(outgoingTransfer_->filename.toStdString());
  net_.sendFrame(MsgType::UploadBlobBegin, w.take());

  setFileTransferStatus(tr("Subiendo: %1").arg(outgoingTransfer_->filename));
  setFileTransferProgress(0.0);
  // fileSendTimer_ arranca al llegar UploadBlobBeginOk con el blobId (ver
  // onFrameReceived).
}

void ClientController::startSelfFileAttach(const QUrl& fileUrl) {
  QString sourcePath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  QFile sourceFile(sourcePath);
  if (!sourceFile.exists()) {
    setErrorText(tr("No se pudo leer el archivo seleccionado."));
    return;
  }

  // Se copia (no se referencia la ruta original) para que el enlace siga
  // funcionando aunque el archivo original se mueva o se borre despues.
  // Se guarda dentro del almacenamiento propio de la app (no Descargas):
  // en Android hace falta que sea una ruta que qtprovider_paths.xml cubra
  // para poder generar un content:// via FileProvider en openSelfFile.
  QString destDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/mis_archivos";
  QDir().mkpath(destDir);
  QString filename = resolveOriginalFilename(fileUrl);
  if (filename.isEmpty()) filename = tr("archivo");
  QString destPath = uniqueDownloadPath(destDir, filename);

  if (!sourceFile.copy(destPath)) {
    setErrorText(tr("No se pudo guardar una copia local del archivo."));
    return;
  }

  QFileInfo destInfo(destPath);
  double mb = static_cast<double>(destInfo.size()) / (1024.0 * 1024.0);
  QString sizeText = QString::number(mb, 'f', mb < 0.1 ? 3 : 1) + " MB";
  // rawHtml=true igual que el enlace de descarga normal -- este SI se
  // puede persistir tal cual, no depende de ninguna clave que solo viva
  // en memoria.
  QString link = "<a href='templar-selffile:" +
                QString::fromLatin1(QUrl::toPercentEncoding(destPath)) + "'>📄 " +
                filename.toHtmlEscaped() + " (" + sizeText + ")</a>";
  logChat(myUsername_, /*Own=*/1, username(), link, /*rawHtml=*/true);
}

void ClientController::openSelfFile(const QString& path) {
#ifdef Q_OS_ANDROID
  QJniObject activity = QNativeInterface::QAndroidApplication::context();
  if (!activity.isValid()) return;

  // "<paquete>.qtprovider" -- misma autoridad que capturePhoto()/
  // CameraHelper.java usan para el mismo <provider> FileProvider.
  QJniObject packageName = activity.callObjectMethod("getPackageName", "()Ljava/lang/String;");
  QString authorityStr = packageName.toString() + ".qtprovider";
  QJniObject jAuthority = QJniObject::fromString(authorityStr);
  QJniObject jPath = QJniObject::fromString(path);

  QJniObject::callStaticMethod<void>(
      "com/templar/phone/FileOpenHelper", "openFile",
      "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V", activity.object(),
      jAuthority.object<jstring>(), jPath.object<jstring>());
#else
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
    setErrorText(tr("No se pudo abrir el archivo: ") + path);
  }
#endif
}

void ClientController::sendNextFileChunk() {
  if (!outgoingTransfer_ || outgoingTransfer_->blobId.empty()) {
    fileSendTimer_->stop();
    return;
  }
  auto& t = *outgoingTransfer_;

  QByteArray buf = t.file->read(kFileChunkSize);
  // Se sabe si es el ultimo fragmento sin "mirar hacia adelante" porque ya
  // conocemos t.totalSize de antemano -- crypto_secretstream necesita
  // saberlo EN EL MOMENTO (para marcarlo TAG_FINAL), no se puede anadir esa
  // marca despues.
  bool isLast = (t.sentBytes + static_cast<quint64>(buf.size())) >= t.totalSize;

  Bytes plainChunk(buf.constData(), buf.constData() + buf.size());
  Bytes cipherChunk = t.encryptor->encryptChunk(plainChunk, isLast);

  Writer w;
  w.str(t.blobId);
  w.blob(cipherChunk);
  net_.sendFrame(MsgType::UploadBlobChunk, w.take());

  t.sentBytes += static_cast<quint64>(buf.size());
  setFileTransferProgress(t.totalSize > 0
                              ? static_cast<double>(t.sentBytes) / static_cast<double>(t.totalSize)
                              : 1.0);

  if (isLast) {
    fileSendTimer_->stop();
    Writer end;
    end.str(t.blobId);
    net_.sendFrame(MsgType::UploadBlobEnd, end.take());
  }
}

void ClientController::sendBlobPointerAndFinish() {
  if (!outgoingTransfer_) return;
  OutgoingTransfer t = std::move(*outgoingTransfer_);
  outgoingTransfer_.reset();
  t.file->close();

  Bytes keyBytes(t.key.begin(), t.key.end());
  Bytes headerBytes(t.encryptor->header().begin(), t.encryptor->header().end());
  Bytes pointerPayload = MessagePayload::encodeFileBlobPointer(t.blobId, t.filename.toStdString(),
                                                                t.totalSize, keyBytes, headerBytes);

  setFileTransferStatus("");
  setFileTransferProgress(0.0);

  if (t.isGroup) {
    // Mismo mecanismo de fan-out que un mensaje de texto de grupo -- la
    // subida ya se hizo UNA vez, esto solo manda un puntero pequeno por
    // miembro.
    logChat(t.peer, /*Own=*/1, username(), tr("Archivo enviado: %1").arg(t.filename));

    QStringList qMembers = conversations_.membersOf(QString::fromStdString(t.peer));
    std::vector<std::string> recipients;
    for (const QString& member : qMembers) {
      if (member.toStdString() != myUsername_) recipients.push_back(member.toStdString());
    }
    activeGroupFanout_ = GroupFanout{t.peer, pointerPayload, std::move(recipients)};
    continueGroupFanout();
  } else if (crypto_.hasSession(t.peer)) {
    Bytes ciphertext = crypto_.encryptNext(t.peer, pointerPayload);
    sendEncryptedToServer(t.peer, ciphertext);
    logChat(t.peer, /*Own=*/1, username(), tr("Archivo enviado: %1").arg(t.filename));
    trySaveSession(t.peer);
  } else {
    // Sesion perdida entre que se empezo la subida y que termino (raro,
    // pero posible si p.ej. se borro el almacen local a mitad) -- se
    // bootstrea igual que un mensaje de texto normal sin sesion.
    pendingOutboundPeer_ = t.peer;
    pendingOutboundPayload_ = pointerPayload;
    pendingOutboundGroupId_.clear();
    pendingOutboundOwnLineText_ = tr("Archivo enviado: %1").arg(t.filename);
    pendingOutboundReplyToSender_.clear();
    pendingOutboundReplyToText_.clear();
    Writer w;
    w.str(t.peer);
    net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
  }
}

void ClientController::onFileBlobPointerReceived(const std::string& originKey,
                                                  const std::string& sender,
                                                  const std::string& blobId,
                                                  const QString& filename, quint64 fileSize,
                                                  const Bytes& fileKey, const Bytes& fileHeader,
                                                  qint64 sentAt) {
  if (fileSize > kMaxFileSize || fileKey.size() != templar::crypto::kFileKeyBytes ||
      fileHeader.size() != templar::crypto::kFileHeaderBytes) {
    setErrorText(tr("Se rechaza un puntero de archivo invalido de %1.")
                     .arg(QString::fromStdString(sender)));
    return;
  }

  PendingBlobDownload pending;
  pending.originKey = originKey;
  pending.sender = sender;
  pending.filename = filename;
  pending.fileSize = fileSize;
  std::copy(fileKey.begin(), fileKey.end(), pending.key.begin());
  std::copy(fileHeader.begin(), fileHeader.end(), pending.header.begin());
  pendingBlobDownloads_[blobId] = std::move(pending);

  if (localStore_.isUnlocked()) {
    try {
      templar::client::PendingBlobDownloadRecord rec;
      rec.blobId = blobId;
      rec.originKey = originKey;
      rec.sender = sender;
      rec.filename = filename.toStdString();
      rec.fileSize = fileSize;
      rec.fileKey = fileKey;
      rec.fileHeader = fileHeader;
      localStore_.savePendingBlobDownload(rec);
    } catch (const std::exception&) {
      // Un fallo persistiendo no debe romper la recepcion en vivo -- mismo
      // criterio que logChat.
    }
  }

  double mb = static_cast<double>(fileSize) / (1024.0 * 1024.0);
  QString sizeText = QString::number(mb, 'f', mb < 0.1 ? 3 : 1) + " MB";
  // rawHtml=true: la unica linea que nosotros mismos generamos con HTML de
  // proposito (ver ChatHistoryModel::RawHtmlRole) -- el nombre del archivo
  // SI se escapa a mano porque ese si viene de otra persona. Se persiste
  // este mismo enlace (no un texto de repuesto): como la clave tambien se
  // guarda arriba, sigue funcionando tras reiniciar la app, hasta que se
  // descargue o el blob caduque en el servidor (30 dias).
  QString link = "<a href='templar-download:" + QString::fromStdString(blobId) + "'>⬇ " +
                tr("Descargar") + " " + filename.toHtmlEscaped() + " (" + sizeText + ")</a>";
  logChat(originKey, /*Peer=*/2, QString::fromStdString(sender), link, /*rawHtml=*/true,
         /*persistText=*/QString(), sentAt);
  if (originKey != activeConversationKey_.toStdString()) markUnread(originKey);
}

void ClientController::startBlobDownload(const QString& blobIdQ) {
  std::string blobId = blobIdQ.toStdString();
  auto it = pendingBlobDownloads_.find(blobId);
  if (it == pendingBlobDownloads_.end()) {
    setErrorText(tr("Ese enlace de descarga ya no esta disponible en esta sesion."));
    return;
  }
  if (activeDownload_) {
    setErrorText(tr("Ya hay una descarga en curso, espera a que termine."));
    return;
  }

  PendingBlobDownload pending = std::move(it->second);
  pendingBlobDownloads_.erase(it);
  // Se borra de LocalStore aqui, al empezar la descarga -- no al terminar
  // con exito -- para que coincida con el borrado de arriba: si falla o
  // llega corrupto, no queda un puntero persistido "zombie" sin entrada en
  // memoria que lo respalde.
  if (localStore_.isUnlocked()) {
    try {
      localStore_.deletePendingBlobDownload(blobId);
    } catch (const std::exception&) {
    }
  }

  // .fileName() descarta cualquier componente de ruta (p.ej.
  // "../../etc/passwd") -- nunca hay que confiar en el nombre que manda el
  // otro lado como una ruta real.
  QString safeName = QFileInfo(pending.filename).fileName();
  if (safeName.isEmpty()) safeName = tr("archivo_recibido");

  auto file = std::make_unique<QFile>();
  bool usesMediaStore = false;
  QString displayName = safeName;

#ifdef Q_OS_ANDROID
  {
    int fd = openMediaStoreFd(isImageFilename(safeName), safeName);
    if (fd >= 0) {
      if (file->open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        usesMediaStore = true;
      } else {
        // No deberia pasar en la practica (abrir un fd recien devuelto por
        // el sistema), pero si pasa hay que borrar la fila "pendiente" que
        // MediaStoreHelper ya inserto para no dejarla huerfana antes de
        // caer al fichero privado de abajo.
        finalizeMediaStoreWrite(/*keep=*/false);
      }
    }
  }
#endif

  if (!usesMediaStore) {
    QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloadsDir);
    QString destPath = uniqueDownloadPath(downloadsDir, safeName);
    file->setFileName(destPath);
    if (!file->open(QIODevice::WriteOnly)) {
      setErrorText(tr("No se pudo crear el archivo de destino para la descarga."));
      return;
    }
    displayName = QFileInfo(destPath).fileName();
  }

  ActiveBlobDownload active;
  active.blobId = blobId;
  active.originKey = pending.originKey;
  active.sender = pending.sender;
  active.filename = displayName;
  active.totalSize = pending.fileSize;
  active.file = std::move(file);
  active.usesMediaStore = usesMediaStore;
  active.decryptor = std::make_unique<templar::crypto::FileDecryptor>(pending.key, pending.header);
  activeDownload_ = std::move(active);

  setFileTransferStatus(tr("Descargando: %1").arg(activeDownload_->filename));
  setFileTransferProgress(0.0);

  Writer w;
  w.str(blobId);
  net_.sendFrame(MsgType::DownloadBlob, w.take());
}

void ClientController::onBlobDataReceived(const std::string& blobId, const Bytes& chunk) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  auto& d = *activeDownload_;

  try {
    templar::crypto::FileDecryptor::Result result = d.decryptor->decryptChunk(chunk);
    d.file->write(reinterpret_cast<const char*>(result.plaintext.data()),
                 static_cast<qint64>(result.plaintext.size()));
    d.receivedBytes += result.plaintext.size();
    if (result.isLast) d.finalTagSeen = true;

    setFileTransferProgress(d.totalSize > 0 ? static_cast<double>(d.receivedBytes) /
                                                  static_cast<double>(d.totalSize)
                                            : 1.0);
  } catch (const std::exception& e) {
    logChat(d.originKey, /*Peer=*/2, QString::fromStdString(d.sender),
           tr("[ALERTA] Fallo descifrando '%1': %2").arg(d.filename, QString::fromUtf8(e.what())));
#ifdef Q_OS_ANDROID
    // Mismo criterio que el fichero privado de siempre: lo ya escrito se
    // queda (solo se avisa), no se borra por un fallo de descifrado a
    // medias -- aqui eso significa aclarar IS_PENDING, no borrar la fila.
    if (d.usesMediaStore) finalizeMediaStoreWrite(/*keep=*/true);
#endif
    activeDownload_.reset();
    setFileTransferStatus("");
  }
}

void ClientController::onBlobEndReceived(const std::string& blobId) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  ActiveBlobDownload d = std::move(*activeDownload_);
  activeDownload_.reset();

  QString savedPath = d.file->fileName();
  d.file->close();

#ifdef Q_OS_ANDROID
  if (d.usesMediaStore) finalizeMediaStoreWrite(/*keep=*/true);
#endif

  setFileTransferStatus("");

  // Sin ruta local que mostrar cuando fue a MediaStore (vive detras de un
  // content:// interno, no de una ruta de fichero) -- se describe el
  // destino en palabras en su lugar.
  QString location =
      d.usesMediaStore ? (isImageFilename(d.filename) ? tr("Galeria") : tr("Descargas")) : savedPath;

  if (d.finalTagSeen) {
    logChat(d.originKey, /*Peer=*/2, QString::fromStdString(d.sender),
           tr("Archivo '%1' recibido y verificado -> %2").arg(d.filename, location));
  } else {
    logChat(d.originKey, /*Peer=*/2, QString::fromStdString(d.sender),
           tr("[ALERTA] El archivo '%1' llego incompleto (se corto en transito) -- no te fies "
              "del contenido.")
               .arg(d.filename));
  }
}

void ClientController::onBlobNotFound(const std::string& blobId, const QString& reason) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  ActiveBlobDownload d = std::move(*activeDownload_);
  activeDownload_.reset();

#ifdef Q_OS_ANDROID
  if (d.usesMediaStore) {
    finalizeMediaStoreWrite(/*keep=*/false);
  } else {
    d.file->remove();
  }
#else
  d.file->remove();
#endif

  setFileTransferStatus("");

  logChat(d.originKey, /*Peer=*/2, QString::fromStdString(d.sender),
         tr("[ALERTA] No se pudo descargar '%1': %2").arg(d.filename, reason));
}

void ClientController::cancelActiveTransfers() {
  fileSendTimer_->stop();
  outgoingTransfer_.reset();
#ifdef Q_OS_ANDROID
  // Mismo criterio de "se queda lo escrito hasta ahora" que el resto de
  // finales no-exitosos con fichero privado (nunca se borro aqui tampoco).
  if (activeDownload_ && activeDownload_->usesMediaStore) finalizeMediaStoreWrite(/*keep=*/true);
#endif
  activeDownload_.reset();
  setFileTransferStatus("");
  setFileTransferProgress(0.0);
}

Bytes ClientController::buildRegisterPayload(const std::string& username,
                                             const std::string& password) const {
  const auto& ex = crypto_.identity().exchangeKeys();
  const auto& ed = crypto_.identity().signingKeys();
  const auto& spk = crypto_.signedPrekey();

  Writer w;
  w.str(username);
  w.str(password);
  w.blob(ex.pk, crypto_box_PUBLICKEYBYTES);
  w.blob(ed.pk, crypto_sign_PUBLICKEYBYTES);
  w.blob(spk.pk, crypto_box_PUBLICKEYBYTES);
  w.blob(crypto_.signedPrekeySignature());
  return w.take();
}

void ClientController::registerAccount(const QString& username, const QString& password) {
  setErrorText("");
  if (username.isEmpty() || password.size() < 8) {
    setErrorText(
        tr("El usuario no puede estar vacio y la contrasena necesita al menos 8 caracteres."));
    return;
  }
  pendingUsername_ = username.toStdString();
  pendingPassword_ = password.toStdString();
  net_.sendFrame(MsgType::Register, buildRegisterPayload(pendingUsername_, pendingPassword_));
}

void ClientController::login(const QString& username, const QString& password) {
  setErrorText("");
  pendingUsername_ = username.toStdString();
  pendingPassword_ = password.toStdString();

  Writer w;
  w.str(pendingUsername_);
  w.str(pendingPassword_);
  net_.sendFrame(MsgType::Login, w.take());

  QSettings().setValue("lastUsername", username);
}

void ClientController::onNetConnected() {
  setConnected(true);
  setStatusText(tr("Conectado."));
  logSystem(tr("Conectado."));
  startBackgroundConnectionService();
}

void ClientController::onNetDisconnected() {
  setConnected(false);
  setLoggedIn(false);
  setStatusText(tr("Desconectado"));
  setUsername("");
  localStore_.lock();
  stopBackgroundConnectionService();

  // ClientController es un unico objeto que vive mientras dure la app --
  // sobrevive a un logout, a diferencia de LocalStore (que se bloquea
  // arriba). Sin esto, los chats/historial/grupos de la cuenta anterior se
  // quedaban pegados en pantalla al iniciar sesion despues con una cuenta
  // distinta en la misma sesion de la app (bug real reportado: se veian
  // chats -- con su contenido -- que no pertenecian a la cuenta nueva).
  conversations_.clear();
  conversationHistory_.clear();
  // "Sistema" se vuelve a anadir de inmediato, vacia -- ver el comentario
  // de kSystemKey sobre por que hace falta volver a registrarla aqui (a
  // diferencia del escritorio, que nunca la borra). El aviso de
  // desconexion se registra aqui, DESPUES de re-anadirla (si fuera antes
  // del clear() de arriba, desaparecia de la vista en caliente al
  // instante) -- a cambio, como el almacen local ya esta bloqueado en
  // este punto, este aviso concreto no sobrevive a un reinicio (aceptable
  // para una notificacion de "se corto la conexion").
  conversations_.upsert(QString::fromLatin1(kSystemKey), tr("Sistema"), false);
  conversations_.setOnline(QString::fromLatin1(kSystemKey), true);
  logSystem(tr("Conexion cerrada."));
  knownGroupKeys_.clear();
  history_.setLines({});
  activeConversationKey_.clear();
  pendingOutboundPeer_.reset();
  pendingOutboundPayload_.clear();
  pendingOutboundGroupId_.clear();
  pendingOutboundOwnLineText_.clear();
  activeGroupFanout_.reset();
  pendingGroupInvitees_.clear();
  pendingInvites_.clear();
  emit pendingInvitesChanged();
  cancelActiveTransfers();
  pendingBlobDownloads_.clear();
  unreadCounts_.clear();
}

void ClientController::onNetError(const QString& message) {
  setErrorText(message);
  logSystem(tr("Error de red: %1").arg(message));
}

void ClientController::onFrameReceived(MsgType type, Bytes payload) {
  try {
    switch (type) {
      case MsgType::RegisterOk: {
        setErrorText("");
        setStatusText(tr("Registro exitoso. Ya puedes iniciar sesion."));
        logSystem(tr("Registro exitoso como %1.").arg(QString::fromStdString(pendingUsername_)));

        // Una cuenta recien registrada NUNCA debe heredar el almacen local
        // de una cuenta anterior con el mismo nombre de usuario -- ver el
        // comentario de resetAndUnlock en LocalStore.hpp.
        try {
          localStore_.resetAndUnlock(pendingUsername_, pendingPassword_);

          PersistedIdentity toSave;
          toSave.ed = crypto_.identity().signingKeys();
          toSave.x = crypto_.identity().exchangeKeys();
          toSave.signedPrekey = crypto_.signedPrekey();
          toSave.signedPrekeySig = crypto_.signedPrekeySignature();
          localStore_.saveIdentity(toSave);
          localStore_.lock();  // todavia no hemos iniciado sesion de verdad
        } catch (const std::exception&) {
          // Un fallo guardando el almacen local no debe impedir que el
          // registro en el servidor cuente como exitoso.
        }
        break;
      }
      case MsgType::RegisterErr: {
        Reader r(payload);
        QString reason = QString::fromStdString(r.str());
        setErrorText(reason);
        logSystem(tr("Error de registro: %1").arg(reason));
        break;
      }
      case MsgType::LoginOk: {
        setUsername(pendingUsername_);
        setErrorText("");
        setLoggedIn(true);
        logSystem(tr("Sesion iniciada como %1.").arg(QString::fromStdString(myUsername_)));

        // Se registra ANTES de loadHistoryFromStore() (mas abajo) para que
        // siempre quede como segunda entrada fija, justo despues de
        // "Sistema" -- upsert no reordena nada despues de la primera vez
        // que se anade una clave.
        conversations_.upsert(QString::fromStdString(myUsername_), tr("Tú"), false);
        // Siempre "en linea" -- eres tu mismo, mismo criterio de
        // simplicidad que kSystemKey de arriba.
        conversations_.setOnline(QString::fromStdString(myUsername_), true);

        UnlockResult unlockResult = localStore_.unlock(myUsername_, pendingPassword_);
        pendingPassword_.assign(pendingPassword_.size(), '\0');
        pendingPassword_.clear();

        if (unlockResult == UnlockResult::WrongPassword) {
          QString msg =
              tr("Sesion iniciada, pero no se pudo desbloquear el almacen local (¿cambiaste la "
                 "contrasena desde otro dispositivo?).");
          setStatusText(msg);
          logSystem(msg);
        } else if (unlockResult == UnlockResult::LoadedExisting) {
          try {
            auto persisted = localStore_.loadIdentity();
            if (persisted) {
              templar::crypto::Identity restoredIdentity(persisted->ed, persisted->x);
              crypto_.restoreIdentity(restoredIdentity, persisted->signedPrekey,
                                      persisted->signedPrekeySig);
            }
            for (const std::string& peer : localStore_.listSessionPeers()) {
              auto session = localStore_.loadSession(peer);
              if (session) crypto_.importSession(peer, *session);
            }
            pendingBlobDownloads_.clear();
            for (const auto& rec : localStore_.loadPendingBlobDownloads()) {
              PendingBlobDownload pending;
              pending.originKey = rec.originKey;
              pending.sender = rec.sender;
              pending.filename = QString::fromStdString(rec.filename);
              pending.fileSize = rec.fileSize;
              if (rec.fileKey.size() == templar::crypto::kFileKeyBytes &&
                  rec.fileHeader.size() == templar::crypto::kFileHeaderBytes) {
                std::copy(rec.fileKey.begin(), rec.fileKey.end(), pending.key.begin());
                std::copy(rec.fileHeader.begin(), rec.fileHeader.end(), pending.header.begin());
                pendingBlobDownloads_[rec.blobId] = std::move(pending);
              }
            }
            loadHistoryFromStore();

            // Solo actualiza el modelo para conversaciones YA listadas
            // aqui (los chats 1-a-1, que loadHistoryFromStore acaba de
            // listar) -- para un grupo, cuyo MyGroups todavia no ha
            // llegado, se aplica mas abajo en ese mismo caso (ver
            // MsgType::MyGroups), igual que updateSidebarUnreadStyle en
            // el escritorio.
            unreadCounts_ = localStore_.loadUnreadCounts();
            for (const auto& [key, count] : unreadCounts_) {
              conversations_.setUnreadCount(QString::fromStdString(key), count);
            }

            setStatusText(tr("Conectado como %1 (almacen local restaurado).")
                              .arg(QString::fromStdString(myUsername_)));
            logSystem(tr("Almacen local desbloqueado: identidad, sesiones e historial restaurados."));
          } catch (const std::exception& e) {
            QString msg = tr("Sesion iniciada, pero fallo restaurando el almacen local: %1")
                              .arg(QString::fromUtf8(e.what()));
            setStatusText(msg);
            logSystem(msg);
          }
        } else {  // CreatedNew
          PersistedIdentity toSave;
          toSave.ed = crypto_.identity().signingKeys();
          toSave.x = crypto_.identity().exchangeKeys();
          toSave.signedPrekey = crypto_.signedPrekey();
          toSave.signedPrekeySig = crypto_.signedPrekeySignature();
          localStore_.saveIdentity(toSave);
          setStatusText(tr("Conectado como %1.").arg(QString::fromStdString(myUsername_)));
        }
        ensureOneTimePrekeys();

        emit loginSucceeded();
        break;
      }
      case MsgType::LoginErr: {
        Reader r(payload);
        QString reason = QString::fromStdString(r.str());
        setErrorText(reason);
        logSystem(tr("Error de inicio de sesion: %1").arg(reason));
        break;
      }
      case MsgType::PresenceUpdate: {
        Reader r(payload);
        std::string peerUsername = r.str();
        bool online = r.u8() != 0;
        conversations_.setOnline(QString::fromStdString(peerUsername), online);
        break;
      }
      case MsgType::GroupCreated: {
        Reader r(payload);
        std::string groupId = r.str();
        std::string name = r.str();
        QString qGroupId = QString::fromStdString(groupId);
        QString myself = QString::fromStdString(myUsername_);
        conversations_.upsert(qGroupId, QString::fromStdString(name), /*isGroup=*/true);
        // Quien crea el grupo queda de admin y unico miembro inicial en el
        // servidor -- los invitados aparecen en members_ solo al llegar su
        // GroupMemberJoined respectivo, no antes.
        conversations_.setGroupInfo(qGroupId, myself, {myself});
        knownGroupKeys_.insert(groupId);
        if (localStore_.isUnlocked()) {
          try {
            localStore_.rememberGroupKey(groupId);
          } catch (const std::exception&) {
          }
        }
        for (const QString& invitee : pendingGroupInvitees_) {
          Writer iw;
          iw.str(groupId);
          iw.str(invitee.toStdString());
          net_.sendFrame(MsgType::InviteToGroup, iw.take());
        }
        pendingGroupInvitees_.clear();
        break;
      }
      case MsgType::GroupErr: {
        Reader r(payload);
        r.str();  // contexto ("create"/"invite"/...) -- no se distingue todavia en la UI movil
        setErrorText(QString::fromStdString(r.str()));
        break;
      }
      case MsgType::MyGroups: {
        Reader r(payload);
        uint32_t count = r.u32();
        conversations_.removeGroups();
        for (uint32_t i = 0; i < count; ++i) {
          std::string groupId = r.str();
          std::string name = r.str();
          std::string admin = r.str();
          uint32_t memberCount = r.u32();
          QStringList members;
          members.reserve(static_cast<int>(memberCount));
          for (uint32_t j = 0; j < memberCount; ++j) members << QString::fromStdString(r.str());
          QString qGroupId = QString::fromStdString(groupId);
          conversations_.upsert(qGroupId, QString::fromStdString(name), /*isGroup=*/true);
          conversations_.setGroupInfo(qGroupId, QString::fromStdString(admin), members);
          knownGroupKeys_.insert(groupId);
          // removeGroups() de mas arriba borro la entrada anterior del
          // grupo (si la habia) -- la entrada nueva empieza con el
          // contador a 0, asi que hay que reaplicarlo aqui desde
          // unreadCounts_ (ver el comentario del caso LoginOk).
          auto ucIt = unreadCounts_.find(groupId);
          if (ucIt != unreadCounts_.end() && ucIt->second > 0) {
            conversations_.setUnreadCount(qGroupId, ucIt->second);
          }
          if (localStore_.isUnlocked()) {
            try {
              localStore_.rememberGroupKey(groupId);
            } catch (const std::exception&) {
            }
          }
        }
        break;
      }
      case MsgType::GroupMemberJoined: {
        Reader r(payload);
        QString groupId = QString::fromStdString(r.str());
        QString joined = QString::fromStdString(r.str());
        conversations_.addMember(groupId, joined);
        emit groupInfoChanged(groupId);
        break;
      }
      case MsgType::GroupMemberKicked: {
        Reader r(payload);
        QString groupId = QString::fromStdString(r.str());
        QString kicked = QString::fromStdString(r.str());
        if (kicked == QString::fromStdString(myUsername_)) {
          conversations_.removeEntry(groupId);
        } else {
          conversations_.removeMember(groupId, kicked);
          emit groupInfoChanged(groupId);
        }
        break;
      }
      case MsgType::GroupMemberLeft: {
        Reader r(payload);
        QString groupId = QString::fromStdString(r.str());
        QString left = QString::fromStdString(r.str());
        QString currentAdmin = QString::fromStdString(r.str());
        conversations_.removeMember(groupId, left);
        conversations_.setAdmin(groupId, currentAdmin);
        emit groupInfoChanged(groupId);
        break;
      }
      case MsgType::PrekeyBundle: {
        if (!pendingOutboundPeer_) break;
        std::string peer = *pendingOutboundPeer_;
        Bytes pointerPayload = pendingOutboundPayload_;
        std::string groupId = pendingOutboundGroupId_;
        QString ownLineText = pendingOutboundOwnLineText_;
        QString replyToSender = pendingOutboundReplyToSender_;
        QString replyToText = pendingOutboundReplyToText_;
        pendingOutboundPeer_.reset();
        pendingOutboundPayload_.clear();
        pendingOutboundGroupId_.clear();
        pendingOutboundOwnLineText_.clear();
        pendingOutboundReplyToSender_.clear();
        pendingOutboundReplyToText_.clear();

        Reader r(payload);
        templar::crypto::PrekeyBundle bundle{};
        Bytes idX = r.blob();
        Bytes idEd = r.blob();
        Bytes spk = r.blob();
        bundle.signedPrekeySig = r.blob();
        Bytes otpk = r.blob();

        bool sizesOk = idX.size() == crypto_box_PUBLICKEYBYTES &&
                      idEd.size() == crypto_sign_PUBLICKEYBYTES &&
                      spk.size() == crypto_box_PUBLICKEYBYTES &&
                      (otpk.empty() || otpk.size() == crypto_box_PUBLICKEYBYTES);
        // A diferencia de una sola cadena de "if falla, break": en un
        // fan-out de grupo un bundle invalido de UN miembro no debe cortar
        // el envio al resto -- por eso esto es un if/else que siempre cae
        // hasta el continueGroupFanout() de mas abajo, en vez de salir del
        // case a mitad camino.
        if (!sizesOk) {
          setErrorText(tr("Bundle de prekeys con tamano invalido, se descarta."));
        } else {
          std::copy(idX.begin(), idX.end(), bundle.identityPkX25519);
          std::copy(idEd.begin(), idEd.end(), bundle.identityPkEd25519);
          std::copy(spk.begin(), spk.end(), bundle.signedPrekeyPub);
          bundle.hasOneTimePrekey = !otpk.empty();
          if (bundle.hasOneTimePrekey) {
            std::copy(otpk.begin(), otpk.end(), bundle.oneTimePrekeyPub);
          }

          if (!bundle.verify()) {
            setErrorText(tr("El bundle de '%1' tiene una firma invalida -- posible "
                            "intermediario. Mensaje NO enviado.")
                             .arg(QString::fromStdString(peer)));
          } else {
            auto first = crypto_.encryptFirst(peer, bundle, pointerPayload);
            Writer w;
            if (groupId.empty()) {
              w.str(peer);
              w.u8(1);
              w.blob(first.senderIdentityPkX25519);
              w.blob(first.senderEphemeralPk);
              w.blob(first.ciphertext);
              w.blob(otpk);
              net_.sendFrame(MsgType::SendMsg, w.take());
              if (!ownLineText.isEmpty()) {
                logChat(peer, /*Own=*/1, username(), ownLineText, /*rawHtml=*/false,
                       /*persistText=*/QString(), /*timestamp=*/0, replyToSender, replyToText);
              }
            } else {
              w.str(groupId);
              w.str(peer);
              w.u8(1);
              w.blob(first.senderIdentityPkX25519);
              w.blob(first.senderEphemeralPk);
              w.blob(first.ciphertext);
              w.blob(otpk);
              net_.sendFrame(MsgType::SendGroupMsg, w.take());
              // La linea propia ya se registro una vez en
              // sendGroupMessage() al arrancar el fan-out.
            }
            trySaveSession(peer);
          }
        }

        if (!groupId.empty()) continueGroupFanout();
        break;
      }
      case MsgType::PrekeyBundleErr: {
        Reader r(payload);
        setErrorText(tr("No se pudo iniciar conversacion: %1").arg(QString::fromStdString(r.str())));
        bool wasGroupFanout = pendingOutboundPeer_ && !pendingOutboundGroupId_.empty();
        pendingOutboundPeer_.reset();
        pendingOutboundPayload_.clear();
        pendingOutboundGroupId_.clear();
        pendingOutboundOwnLineText_.clear();
        pendingOutboundReplyToSender_.clear();
        pendingOutboundReplyToText_.clear();
        if (wasGroupFanout) continueGroupFanout();
        break;
      }
      case MsgType::UploadBlobBeginOk: {
        if (!outgoingTransfer_) break;
        Reader r(payload);
        outgoingTransfer_->blobId = r.str();
        fileSendTimer_->start(0);  // ya tenemos blobId, ahora si se puede empezar a mandar
        break;
      }
      case MsgType::UploadBlobBeginErr: {
        Reader r(payload);
        setErrorText(
            tr("No se pudo empezar a subir el archivo: %1").arg(QString::fromStdString(r.str())));
        outgoingTransfer_.reset();
        setFileTransferStatus("");
        break;
      }
      case MsgType::UploadBlobEndOk:
        sendBlobPointerAndFinish();
        break;
      case MsgType::UploadBlobEndErr: {
        Reader r(payload);
        setErrorText(
            tr("Fallo terminando de subir el archivo: %1").arg(QString::fromStdString(r.str())));
        outgoingTransfer_.reset();
        setFileTransferStatus("");
        break;
      }
      case MsgType::BlobData: {
        Reader r(payload);
        std::string blobId = r.str();
        Bytes chunk = r.blob();
        onBlobDataReceived(blobId, chunk);
        break;
      }
      case MsgType::BlobEnd: {
        Reader r(payload);
        std::string blobId = r.str();
        onBlobEndReceived(blobId);
        break;
      }
      case MsgType::BlobNotFound: {
        Reader r(payload);
        std::string blobId = r.str();
        std::string reason = r.str();
        onBlobNotFound(blobId, QString::fromStdString(reason));
        break;
      }
      case MsgType::DeliverMsg: {
        Reader r(payload);
        uint32_t mailboxId = r.u32();
        std::string sender = r.str();
        bool isFirst = r.u8() != 0;
        Bytes senderIdentityPkX25519 = r.blob();
        Bytes senderEphemeralPk = r.blob();
        Bytes ciphertext = r.blob();
        Bytes usedOneTimePrekeyPub = r.blob();
        auto sentAt = static_cast<qint64>(r.u64());

        // El descifrado/procesado va en su PROPIO try -- si falla (sesion
        // desincronizada, mensaje corrupto, o un reenvio del servidor de
        // un mensaje que ya procesamos en un login anterior, ver
        // Router::flushMailbox en el servidor), el Ack de mas abajo tiene
        // que mandarse IGUAL. Si no, el servidor considera el mensaje
        // "pendiente" para siempre y lo reenvia en cada login futuro,
        // fallando exactamente igual cada vez -- un mensaje irrecuperable
        // se queda atascado en un bucle infinito en vez de descartarse.
        try {
          Bytes plaintext;
          if (isFirst) {
            auto otpk = takeMatchingOneTimePrekey(usedOneTimePrekeyPub);
            plaintext = crypto_.decryptFirst(sender, senderIdentityPkX25519, senderEphemeralPk,
                                             ciphertext, otpk ? &*otpk : nullptr);
          } else {
            plaintext = crypto_.decryptNext(sender, ciphertext);
          }

          DecodedPayload decoded = MessagePayload::decode(plaintext);
          switch (decoded.kind) {
            case PayloadKind::Text:
            case PayloadKind::TextReply: {
              trySaveSession(sender);
              QString qsender = QString::fromStdString(sender);
              bool isNew = conversations_.upsert(qsender, qsender, /*isGroup=*/false);
              if (isNew) subscribePresence(qsender);
              logChat(sender, /*Peer=*/2, qsender, QString::fromStdString(decoded.text),
                     /*rawHtml=*/false, /*persistText=*/QString(), sentAt,
                     QString::fromStdString(decoded.replyToSender),
                     QString::fromStdString(decoded.replyToText));
              if (sender != activeConversationKey_.toStdString()) markUnread(sender);
              if (shouldNotify(qsender)) showSystemNotification(qsender, tr("Nuevo mensaje"));
              break;
            }
            case PayloadKind::FileBlobPointer:
              trySaveSession(sender);
              onFileBlobPointerReceived(sender, sender, decoded.blobId,
                                       QString::fromStdString(decoded.filename),
                                       decoded.fileSize, decoded.fileKey, decoded.fileHeader,
                                       sentAt);
              if (shouldNotify(QString::fromStdString(sender))) {
                showSystemNotification(QString::fromStdString(sender), tr("Nuevo archivo"));
              }
              break;
            case PayloadKind::FileMeta:
            case PayloadKind::FileChunk:
            case PayloadKind::FileEnd:
              // Formato viejo (archivo trocito a trocito por el Double
              // Ratchet) -- ya no se genera, pero un mensaje de este tipo
              // podria seguir en el buzon de alguien que no ha hecho login
              // desde antes del cambio. Se ignora sin romper nada (llegar
              // aqui solo significa perder ese archivo concreto, no el resto
              // de la conversacion).
              trySaveSession(sender);
              break;
          }
        } catch (const std::exception& e) {
          setErrorText(tr("No se pudo descifrar un mensaje de '%1': %2")
                           .arg(QString::fromStdString(sender), QString::fromUtf8(e.what())));
        }

        Writer ack;
        ack.u32(mailboxId);
        net_.sendFrame(MsgType::Ack, ack.take());
        break;
      }
      case MsgType::DeliverGroupMsg: {
        Reader r(payload);
        uint32_t mailboxId = r.u32();
        std::string groupId = r.str();
        std::string sender = r.str();
        bool isFirst = r.u8() != 0;
        Bytes senderIdentityPkX25519 = r.blob();
        Bytes senderEphemeralPk = r.blob();
        Bytes ciphertext = r.blob();
        Bytes usedOneTimePrekeyPub = r.blob();
        auto sentAt = static_cast<qint64>(r.u64());

        // Mismo motivo que en DeliverMsg de arriba: el Ack tiene que
        // mandarse pase lo que pase con el descifrado, o el servidor
        // reintenta este mensaje en bucle en cada login futuro.
        try {
          Bytes plaintext;
          if (isFirst) {
            auto otpk = takeMatchingOneTimePrekey(usedOneTimePrekeyPub);
            plaintext = crypto_.decryptFirst(sender, senderIdentityPkX25519, senderEphemeralPk,
                                             ciphertext, otpk ? &*otpk : nullptr);
          } else {
            plaintext = crypto_.decryptNext(sender, ciphertext);
          }
          trySaveSession(sender);

          DecodedPayload decoded = MessagePayload::decode(plaintext);
          if (decoded.kind == PayloadKind::Text || decoded.kind == PayloadKind::TextReply) {
            logChat(groupId, /*Peer=*/2, QString::fromStdString(sender),
                   QString::fromStdString(decoded.text), /*rawHtml=*/false,
                   /*persistText=*/QString(), sentAt, QString::fromStdString(decoded.replyToSender),
                   QString::fromStdString(decoded.replyToText));
            if (groupId != activeConversationKey_.toStdString()) markUnread(groupId);
            QString qGroupId = QString::fromStdString(groupId);
            if (shouldNotify(qGroupId)) {
              showSystemNotification(conversations_.nameOf(qGroupId), tr("Nuevo mensaje"));
            }
          } else if (decoded.kind == PayloadKind::FileBlobPointer) {
            onFileBlobPointerReceived(groupId, sender, decoded.blobId,
                                     QString::fromStdString(decoded.filename), decoded.fileSize,
                                     decoded.fileKey, decoded.fileHeader, sentAt);
            QString qGroupId = QString::fromStdString(groupId);
            if (shouldNotify(qGroupId)) {
              showSystemNotification(conversations_.nameOf(qGroupId),
                                     tr("Nuevo archivo de %1").arg(QString::fromStdString(sender)));
            }
          }
          // FileMeta/FileChunk/FileEnd (formato viejo): se ignoran, ver el
          // comentario equivalente en el case DeliverMsg.
        } catch (const std::exception& e) {
          setErrorText(tr("No se pudo descifrar un mensaje de grupo de '%1': %2")
                           .arg(QString::fromStdString(sender), QString::fromUtf8(e.what())));
        }

        Writer ack;
        ack.u32(mailboxId);
        net_.sendFrame(MsgType::Ack, ack.take());
        break;
      }
      case MsgType::GroupInvite: {
        Reader r(payload);
        uint32_t inviteId = r.u32();
        r.str();  // groupId: no hace falta guardarlo, Accept/RejectGroupInvite solo mandan inviteId
        std::string groupName = r.str();
        std::string inviter = r.str();

        QVariantMap entry;
        entry["inviteId"] = static_cast<quint32>(inviteId);
        entry["groupName"] = QString::fromStdString(groupName);
        entry["inviter"] = QString::fromStdString(inviter);
        pendingInvites_.push_back(entry);
        emit pendingInvitesChanged();
        break;
      }
      default:
        break;
    }
  } catch (const std::exception& e) {
    setErrorText(tr("Error procesando mensaje del servidor: %1").arg(QString::fromUtf8(e.what())));
  }
}

}  // namespace templar::phone
