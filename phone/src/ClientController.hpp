#pragma once

#include <sodium.h>

#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef Q_OS_ANDROID
#include <jni.h>
#endif

#include "ChatHistoryModel.hpp"
#include "ConversationListModel.hpp"
#include "CryptoEngine.hpp"
#include "LocalStore.hpp"
#include "MessagePayload.hpp"
#include "NetworkManager.hpp"
#include "templar/Protocol.hpp"
#include "templar/crypto/FileCrypto.hpp"

namespace templar::phone {

// Backend puro (QObject normal, nada de Qt Widgets) que expone a QML lo
// mismo que MainWindow orquesta en el cliente de escritorio para conectar,
// registrar e iniciar sesion -- ver client/src/MainWindow.cpp
// (onConnectClicked/onRegisterClicked/onLoginClicked y los casos
// RegisterOk/RegisterErr/LoginOk/LoginErr de onFrameReceived) para el
// equivalente linea a linea. QML solo lee propiedades y llama a metodos
// invocables; toda la logica de red/cripto/persistencia vive aqui, no en
// las paginas .qml.
//
// Reusa tal cual NetworkManager/CryptoEngine/LocalStore de client/src/ --
// ninguna de las tres toca Qt Widgets, asi que no hizo falta adaptar nada.
class ClientController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
  Q_PROPERTY(bool loggedIn READ isLoggedIn NOTIFY loggedInChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
  Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
  // "host:puerto" ya normalizado (con el puerto por defecto aplicado si no
  // se escribio ninguno) -- lo que de verdad se le pidio a NetworkManager,
  // no el texto en crudo que haya escrito el usuario. Vacio mientras no se
  // haya intentado conectar todavia.
  Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY serverAddressChanged)
  // QObject* en vez del tipo concreto: es lo que QML/ListView esperan para
  // usarlo como "model" sin tener que registrar ConversationListModel como
  // tipo QML aparte.
  Q_PROPERTY(QObject* conversations READ conversationsObject CONSTANT)
  // Historial de la conversacion activa (ver setActiveConversation) --
  // mismo criterio de QObject* que conversations de arriba.
  Q_PROPERTY(QObject* history READ historyObject CONSTANT)
  // Invitaciones a grupo pendientes de aceptar/rechazar -- equivalente
  // movil del panel "Invitaciones pendientes" de la barra lateral en
  // escritorio (inviteList_ en MainWindow.hpp), pero como QVariantList de
  // QVariantMap {inviteId, groupName, inviter} en vez de
  // QListWidgetItem+userData, para que ChatListPage.qml lo muestre con un
  // Repeater normal.
  Q_PROPERTY(QVariantList pendingInvites READ pendingInvites NOTIFY pendingInvitesChanged)
  // Estado de la transferencia de archivo en curso (de entrada o de
  // salida, solo puede haber una a la vez -- ver outgoingTransfer_/
  // incomingTransfer_) -- equivalente movil de transferLabel_/
  // transferProgress_ en el escritorio. Texto vacio significa "sin
  // transferencia activa".
  Q_PROPERTY(QString fileTransferStatus READ fileTransferStatus NOTIFY fileTransferStatusChanged)
  Q_PROPERTY(double fileTransferProgress READ fileTransferProgress NOTIFY fileTransferProgressChanged)

 public:
  explicit ClientController(QObject* parent = nullptr);
  // Necesario para poder desconectar net_ ANTES de que empiecen a
  // destruirse el resto de miembros -- ver el comentario junto a la
  // implementacion en el .cpp.
  ~ClientController() override;

  bool isConnected() const { return connected_; }
  bool isLoggedIn() const { return loggedIn_; }
  QString statusText() const { return statusText_; }
  QString errorText() const { return errorText_; }
  QString username() const { return QString::fromStdString(myUsername_); }
  QString serverAddress() const { return serverAddress_; }
  QObject* conversationsObject() { return &conversations_; }
  QObject* historyObject() { return &history_; }
  QVariantList pendingInvites() const { return pendingInvites_; }
  QString fileTransferStatus() const { return fileTransferStatus_; }
  double fileTransferProgress() const { return fileTransferProgress_; }

  // hostAndPort acepta "host:puerto" (el campo unico que usa LoginPage.qml,
  // a diferencia de los dos campos separados del escritorio) -- si no trae
  // puerto valido, usa 8080 (el mismo por defecto que templar_server). El
  // parseo vive aqui, no en QML, a proposito: la vista no debe tener logica.
  Q_INVOKABLE void connectToServer(const QString& hostAndPort);
  Q_INVOKABLE void disconnectFromServer();
  Q_INVOKABLE void registerAccount(const QString& username, const QString& password);
  Q_INVOKABLE void login(const QString& username, const QString& password);

  // Ultimo servidor/usuario usados, recordados via QSettings para que
  // LoginPage.qml los precargue al abrir la app -- la contrasena NUNCA se
  // guarda aqui (ni en ningun otro sitio): la clave de cifrado del propio
  // LocalStore se deriva de ella, asi que guardarla dentro de algo que la
  // necesita para desbloquearse no tendria sentido, y hacerlo aparte
  // (Android Keystore) es una pieza de trabajo distinta y mayor, fuera de
  // alcance por ahora.
  Q_INVOKABLE QString rememberedServerAddress() const;
  Q_INVOKABLE QString rememberedUsername() const;

  // Anade un chat 1-a-1 a la lista (si no estaba ya) y se suscribe a su
  // presencia. No valida contra el servidor que el usuario exista -- mismo
  // criterio que MainWindow::onNewChatClicked en el escritorio, la
  // comprobacion real llega mas adelante al intentar mandar el primer
  // mensaje (X3DH/FetchPrekeyBundle).
  Q_INVOKABLE void startChat(const QString& peerUsername);

  // Crea un grupo con `name` e invita a `inviteUsernames` en cuanto el
  // servidor confirma que existe (ver GroupCreated en onFrameReceived) --
  // mismo orden que MainWindow::onCreateGroupClicked/CreateGroupDialog.
  Q_INVOKABLE void createGroup(const QString& name, const QStringList& inviteUsernames);

  // Invita/expulsa a un miembro de un grupo ya existente -- mismo par de
  // acciones que addMemberButton_/kickButton_ en el escritorio
  // (MainWindow::onAddMemberClicked/onKickClicked), pero sin la
  // comprobacion de que soy admin: el servidor la aplica igualmente, y la
  // UI ya oculta estos botones a quien no lo sea (ver
  // ChatPage.qml::isGroupAdmin).
  Q_INVOKABLE void inviteToGroup(const QString& groupKey, const QString& username);
  Q_INVOKABLE void kickFromGroup(const QString& groupKey, const QString& username);

  // Cualquier miembro puede salir de un grupo, no solo el admin (si el
  // admin sale, el servidor promueve automaticamente al mas antiguo de los
  // que quedan -- ver GroupMemberLeft) -- mismo criterio que
  // MainWindow::onLeaveGroupClicked en el escritorio. Se refleja ya en la
  // UI sin esperar confirmacion del servidor, igual que alli.
  Q_INVOKABLE void leaveGroup(const QString& groupKey);

  // Lecturas puntuales para que QML decida que mostrar (botones +/- de
  // ChatPage.qml, lista de candidatos de KickMemberDialog.qml) sin tener
  // que exponer ConversationListModel entero como tipo QML aparte.
  Q_INVOKABLE QString groupAdmin(const QString& groupKey) const;
  Q_INVOKABLE QStringList groupMembers(const QString& groupKey) const;

  // Acepta/rechaza una invitacion a grupo (ver pendingInvites) -- mismo
  // par que AcceptGroupInvite/RejectGroupInvite en el escritorio
  // (respondToGroupInvite en MainWindow.cpp). Al aceptar, el servidor
  // manda un MyGroups solo a esta sesion con el grupo ya incluido -- no
  // hace falta actualizar conversations_ a mano aqui, el caso MyGroups de
  // onFrameReceived ya lo hace.
  Q_INVOKABLE void acceptGroupInvite(quint32 inviteId);
  Q_INVOKABLE void rejectGroupInvite(quint32 inviteId);

  // Fija que conversacion muestra `history` ahora mismo y recarga su
  // contenido desde memoria -- ChatPage.qml lo llama en
  // Component.onCompleted con su propio peerKey. Solo tiene sentido para
  // chats 1-a-1 por ahora (ver sendMessage).
  Q_INVOKABLE void setActiveConversation(const QString& key);

  // Se llama al salir de un chat (volver a ChatListPage) -- sin esto,
  // activeConversationKey_ se quedaba fijo en el ultimo chat abierto para
  // siempre (nada lo limpiaba al navegar hacia atras), asi que cualquier
  // mensaje nuevo de esa misma persona seguia contando como "ya la estas
  // viendo" -- ni marcaba no leido ni el contador volvia a aparecer, ni
  // aunque hubieras vuelto a la lista o cerrado la app del todo.
  Q_INVOKABLE void clearActiveConversation();

  // Manda un mensaje de texto a un chat 1-a-1 -- si ya hay sesion de
  // Double Ratchet con ese peer, cifra directo (encryptNext); si no, pide
  // su prekey bundle al servidor y completa el envio (X3DH/encryptFirst)
  // en cuanto llega (ver el caso PrekeyBundle en onFrameReceived). No
  // sirve para grupos -- ver sendGroupMessage.
  Q_INVOKABLE void sendMessage(const QString& peerKey, const QString& text);

  // Manda un mensaje de texto a un grupo -- equivalente movil de
  // MainWindow::sendGroupMessage en el escritorio. El cifrado sigue siendo
  // puramente 1-a-1 (ver el comentario de SendGroupMsg en Protocol.hpp): se
  // manda un envio independiente por cada miembro, cada uno cifrado con la
  // sesion de Double Ratchet que ya exista (o se bootstree) con esa
  // persona. Ver activeGroupFanout_/continueGroupFanout.
  Q_INVOKABLE void sendGroupMessage(const QString& groupKey, const QString& text);

  // Manda un archivo a un chat 1-a-1 o a un grupo -- equivalente movil de
  // MainWindow::startOutgoingFileTransfer. El archivo se cifra UNA vez con
  // una clave propia y se sube UNA vez al servidor como blob opaco; solo
  // un puntero pequeno (FileBlobPointer) viaja por el canal cifrado de
  // cada destinatario (o de cada miembro, en fan-out, si es un grupo) --
  // ver el comentario de UploadBlobBegin en Protocol.hpp. Ya no hace falta
  // que el destinatario este en linea: el puntero se pone en cola normal
  // si esta desconectado, igual que un mensaje de texto. Solo una
  // transferencia -- de entrada o de salida -- a la vez en toda la app.
  // fileUrl es la url que entrega FileDialog en QML (soporta content://
  // en Android ademas de file:// normal).
  Q_INVOKABLE void sendFile(const QString& peerKey, const QUrl& fileUrl);

  // Se llama al pulsar el enlace "Descargar" de un mensaje FileBlobPointer
  // en ChatPage.qml (ver ChatHistoryModel::RawHtmlRole). No hace nada si
  // blobId no esta en pendingBlobDownloads_ -- p.ej. si la app se
  // reinicio desde que llego el puntero (la clave solo vive en memoria a
  // proposito, ver el comentario junto a pendingBlobDownloads_).
  Q_INVOKABLE void startBlobDownload(const QString& blobId);

  // Se llama al pulsar un enlace de un archivo adjuntado en el chat
  // CONTIGO MISMO (ver startSelfFileAttach) -- a diferencia de un
  // FileBlobPointer normal, esto no descarga nada, solo abre un archivo
  // que ya esta en el dispositivo. En Android usa FileProvider (mismo
  // mecanismo que CameraHelper.java) para poder pasarselo a otra app
  // (visor de PDF, galeria, etc.); en el resto de plataformas usa
  // QDesktopServices::openUrl directo.
  Q_INVOKABLE void openSelfFile(const QString& path);

  // Saca una foto con la app de camara del sistema (via CameraHelper.java/
  // TemplarActivity.java) y, si sale bien, la manda con el mismo sendFile()
  // de arriba -- mismas restricciones (1-a-1, peer online, sin
  // transferencia ya en curso), solo cambia de donde sale el archivo. No
  // hace nada en plataformas que no sean Android.
  Q_INVOKABLE void capturePhoto(const QString& peerKey);

 signals:
  void connectedChanged();
  void loggedInChanged();
  void statusTextChanged();
  void errorTextChanged();
  void usernameChanged();
  void serverAddressChanged();
  // Distinta de loggedInChanged: se emite solo la vez que un login termina
  // bien, para que QML pueda navegar a ChatListPage sin tener que vigilar
  // `loggedIn` a mano con un Connections/onLoggedInChanged.
  void loginSucceeded();

  // Admin y/o miembros de `groupKey` acaban de cambiar (alguien se unio,
  // fue expulsado, salio, o el admin se reasigno). ChatPage.qml escucha
  // esto para refrescar isGroupAdmin en caliente, ya que ese valor se lee
  // con groupAdmin()/groupMembers() (llamadas puntuales, no propiedades
  // NOTIFYables) y QML no detectaria el cambio por si solo.
  void groupInfoChanged(const QString& groupKey);
  void pendingInvitesChanged();
  void fileTransferStatusChanged();
  void fileTransferProgressChanged();

 private slots:
  void onNetConnected();
  void onNetDisconnected();
  void onNetError(const QString& message);
  void onFrameReceived(templar::proto::MsgType type, templar::proto::Bytes payload);
  // Conectado a fileSendTimer_->timeout() -- lee y manda un fragmento de
  // outgoingTransfer_ cada vez, equivalente movil de
  // MainWindow::sendNextFileChunk.
  void sendNextFileChunk();

 private:
  // Clave de la conversacion "Sistema" -- equivalente movil de
  // MainWindow::kSystemKey en el escritorio. A diferencia del escritorio
  // (donde se registra una unica vez en el constructor y nunca se borra),
  // aqui hay que volver a registrarla tras cada onNetDisconnected() porque
  // ClientController limpia conversations_ entero en cada desconexion
  // (ver el comentario ahi).
  static constexpr const char* kSystemKey = "";

  void setConnected(bool value);
  void setLoggedIn(bool value);
  void setStatusText(const QString& text);
  void setErrorText(const QString& text);
  void setUsername(const std::string& value);
  void setServerAddress(const QString& value);
  void subscribePresence(const QString& peerUsername);
  void respondToGroupInvite(quint32 inviteId, bool accept);

  // Mismo par que markUnread/clearUnread en el escritorio -- incrementa/
  // limpia el contador tanto en memoria (unreadCounts_, para grupos que
  // todavia no estan listados en conversations_) como en el modelo (para
  // que ChatListPage.qml lo pinte) y en LocalStore (para que sobreviva a
  // cerrar la app).
  void markUnread(const std::string& key);
  void clearUnread(const std::string& key);

  templar::proto::Bytes buildRegisterPayload(const std::string& username,
                                             const std::string& password) const;

  // Anade `line` al historial en memoria de `peerKey`, la persiste en
  // LocalStore, y si esa conversacion es la activa la refleja tambien en
  // `history_` en caliente -- equivalente movil de MainWindow::logChat.
  // persistText: si no esta vacio, es lo que se guarda en LocalStore EN
  // VEZ de `text` -- solo hace falta cuando text es HTML crudo
  // (rawHtml=true) cuyo enlace ya no funcionara tras un reinicio (ver
  // pendingBlobDownloads_), para no dejar markup sin escapar en el
  // historial recargado.
  void logChat(const std::string& peerKey, int kind, const QString& who, const QString& text,
              bool rawHtml = false, const QString& persistText = QString());
  // Anade una linea a la conversacion "Sistema" -- equivalente movil de
  // MainWindow::logSystem. Solo se llama para eventos importantes
  // (conectar/desconectar/login/registro/errores), no para cada
  // setStatusText/setErrorText que ya existe en el codigo.
  void logSystem(const QString& text);

  // Adjuntar un archivo en el chat CONTIGO MISMO (peer == myUsername_) no
  // pasa por el servidor -- se copia a una carpeta propia de la app (para
  // que sobreviva aunque el original se mueva o se borre) y se deja un
  // enlace que lo abre con la app del sistema via FileProvider (ver
  // FileOpenHelper.java) -- equivalente movil de
  // MainWindow::startSelfFileAttach.
  void startSelfFileAttach(const QUrl& fileUrl);

  // Termina de armar y manda el SendMsg de un mensaje ya cifrado con una
  // sesion YA establecida (isFirst=0) -- equivalente movil de
  // MainWindow::sendEncryptedToServer.
  void sendEncryptedToServer(const std::string& peer, const templar::proto::Bytes& ciphertext);

  // Mismo par que sendEncryptedToServer/continueGroupFanout de arriba, pero
  // para un unico envio del fan-out de un grupo (SendGroupMsg en vez de
  // SendMsg, con el groupId por delante) -- equivalente movil de
  // MainWindow::sendGroupCiphertextToServer.
  void sendGroupCiphertextToServer(const std::string& groupId, const std::string& recipient,
                                   const templar::proto::Bytes& ciphertext);

  // Procesa la cola de miembros pendientes de activeGroupFanout_ hasta
  // vaciarla o hasta topar con un miembro sin sesion todavia (en cuyo caso
  // pide su prekey bundle y se PARA: el protocolo solo permite una
  // peticion FetchPrekeyBundle en vuelo a la vez, se reanuda desde el caso
  // PrekeyBundle/PrekeyBundleErr de onFrameReceived) -- equivalente movil
  // de MainWindow::continueGroupFanout.
  void continueGroupFanout();

  // Guarda el estado del ratchet con `peer` en LocalStore tras cifrar o
  // descifrar -- equivalente movil de MainWindow::trySaveSession. Nunca
  // propaga una excepcion: un fallo guardando no debe romper el
  // envio/recepcion en vivo.
  void trySaveSession(const std::string& peer);

  // Busca en LocalStore la one-time prekey privada cuya publica coincide
  // con la que el remitente dice haber usado, y la consume (borra) si la
  // encuentra -- equivalente movil de MainWindow::takeMatchingOneTimePrekey.
  std::optional<templar::crypto::X25519KeyPair> takeMatchingOneTimePrekey(
      const templar::proto::Bytes& usedOneTimePrekeyPub);

  // Repone el pool local de one-time prekeys hasta el objetivo si esta por
  // debajo del minimo, y republica el pool completo -- equivalente movil
  // de MainWindow::ensureOneTimePrekeys. Se llama una vez tras cada login
  // exitoso.
  void ensureOneTimePrekeys();

  // Repuebla conversations_/conversationHistory_ desde LocalStore tras un
  // login con almacen existente -- equivalente movil de
  // MainWindow::loadHistoryFromStore. Los grupos conocidos (via
  // loadKnownGroupKeys) se saltan aqui porque MyGroups ya los repuebla por
  // su cuenta con nombre/admin/miembros frescos del servidor.
  void loadHistoryFromStore();

  // --- Transferencia de archivos -- equivalentes moviles uno a uno de las
  // funciones homonimas en MainWindow.cpp del escritorio (ver el
  // comentario de sendFile para el mecanismo). ---
  void sendBlobPointerAndFinish();
  void onFileBlobPointerReceived(const std::string& originKey, const std::string& sender,
                                 const std::string& blobId, const QString& filename,
                                 quint64 fileSize, const templar::proto::Bytes& fileKey,
                                 const templar::proto::Bytes& fileHeader);
  void onBlobDataReceived(const std::string& blobId, const templar::proto::Bytes& chunk);
  void onBlobEndReceived(const std::string& blobId);
  void onBlobNotFound(const std::string& blobId, const QString& reason);
  void cancelActiveTransfers();
  void setFileTransferStatus(const QString& status);
  void setFileTransferProgress(double value);

  // --- Notificaciones nativas de Android -- equivalente movil de
  // trayIcon_/shouldNotify() en el escritorio. No hay QSystemTrayIcon en
  // Android, asi que esto llama via JNI a NotificationHelper.java (ver
  // phone/android/src/com/templar/phone/NotificationHelper.java); en
  // cualquier otra plataforma showSystemNotification() no hace nada. ---

  // Crea el canal de notificaciones y pide el permiso POST_NOTIFICATIONS
  // (obligatorio desde Android 13) -- se llama una vez, al arrancar.
  void setupAndroidNotifications();

  // A diferencia de MainWindow::shouldNotify() en el escritorio (que solo
  // mira si la ventana tiene el foco, nunca que conversacion esta activa):
  // en movil, con una sola conversacion en pantalla a la vez, avisar de un
  // mensaje que ya se esta viendo en directo es mas molesto que util. Se
  // notifica salvo que la app este en primer plano Y `conversationKey`
  // (el remitente, o el groupId) sea justo la conversacion abierta ahora.
  bool shouldNotify(const QString& conversationKey) const;

  // Contenido siempre generico a proposito (title = quien manda, text =
  // "Nuevo mensaje"/"Enviando un archivo", NUNCA el texto real) -- mismo
  // criterio de privacidad que trayIcon_->showMessage() en el escritorio,
  // por si la notificacion queda visible en la pantalla de bloqueo.
  void showSystemNotification(const QString& title, const QString& text);

  // Arranca/para ConnectionService (foreground service vacio, ver
  // phone/android/src/com/templar/phone/ConnectionService.java) -- sin
  // esto Android corta la red de la app pocos segundos despues de
  // minimizarla, matando el socket de NetworkManager aunque el proceso
  // siga vivo. Se arranca al conectar y se para al desconectar, para que
  // la notificacion persistente ("Conectado en segundo plano") solo se
  // vea mientras de verdad hay una conexion que mantener viva.
  void startBackgroundConnectionService();
  void stopBackgroundConnectionService();

  // Llamado desde el metodo native de CameraHelper.java cuando la app de
  // camara del sistema devuelve el control -- si success es true, manda el
  // archivo ya capturado (en pendingPhotoPeerKey_) con sendFile().
  void onPhotoCaptureFinished(const QString& path, bool success);

  // pendingPhotoPeerKey_: a que chat va la foto que se esta capturando --
  // hace falta guardarlo porque capturePhoto() vuelve de inmediato (la app
  // de camara del sistema corre en su propio proceso/actividad) y el
  // resultado llega mas tarde, en otra llamada por completo.
  QString pendingPhotoPeerKey_;

  // Puente hacia el callback estatico de JNI (registrado una vez via
  // QJniEnvironment::registerNativeMethods) -- solo puede haber una
  // ClientController viva en toda la app (creada una vez en main.cpp), asi
  // que un puntero estatico basta para que ese callback (que no tiene
  // "this" propio) sepa a quien avisar.
  static ClientController* instance_;

#ifdef Q_OS_ANDROID
  // Firma JNI exacta que espera CameraHelper.java para su metodo
  // "native static void nativeOnPhotoCaptured(String, boolean)".
  static void onPhotoCapturedJni(JNIEnv* env, jclass clazz, jstring path, jboolean success);
#endif

  templar::client::NetworkManager net_;
  templar::client::CryptoEngine crypto_;
  templar::client::LocalStore localStore_;
  ConversationListModel conversations_;
  ChatHistoryModel history_;

  // Historial completo de cada conversacion 1-a-1, en memoria -- equivalente
  // movil de conversations_ (el mapa, no el modelo QML de arriba) en
  // MainWindow.hpp. `history_` solo refleja la entrada de
  // activeConversationKey_.
  std::unordered_map<std::string, std::vector<ChatLine>> conversationHistory_;
  QString activeConversationKey_;

  // Ids de grupo recordados via LocalStore::rememberGroupKey -- para que
  // loadHistoryFromStore() no confunda un grupo con historial local con un
  // chat 1-a-1 antes de que llegue el MyGroups del servidor. Ver el
  // comentario de rememberGroupKey en LocalStore.hpp.
  std::unordered_set<std::string> knownGroupKeys_;

  // Peer y payload YA CODIFICADO (ver MessagePayload::encode*) de un envio
  // esperando el prekey bundle del servidor (ver
  // sendMessage/sendGroupMessage/sendBlobPointerAndFinish/PrekeyBundle) --
  // equivalente movil de PendingOutbound{peer, groupId, payload,
  // ownLineText} en MainWindow.hpp. pendingOutboundGroupId_ vacio
  // significa "es un envio 1-a-1 normal"; no vacio significa "este
  // miembro es un paso del fan-out de ese grupo".
  std::optional<std::string> pendingOutboundPeer_;
  templar::proto::Bytes pendingOutboundPayload_;
  std::string pendingOutboundGroupId_;
  // Que mostrar en el historial PROPIO cuando este envio 1-a-1 complete
  // (solo se usa si pendingOutboundGroupId_ esta vacio -- en un fan-out de
  // grupo la linea propia ya se registro una vez al arrancar el fan-out).
  QString pendingOutboundOwnLineText_;

  // Fan-out en curso de un mensaje de grupo (uno o ninguno a la vez, igual
  // que en el escritorio) -- equivalente movil de GroupFanout/
  // activeGroupFanout_ en MainWindow.hpp.
  struct GroupFanout {
    std::string groupId;
    // Payload ya codificado -- mismo criterio que pendingOutboundPayload_.
    templar::proto::Bytes payload;
    std::vector<std::string> remainingMembers;
  };
  std::optional<GroupFanout> activeGroupFanout_;

  // Invitados marcados al llamar a createGroup(): se guardan aqui hasta
  // que llega GroupCreated con el id real del grupo ya creado, momento en
  // el que se manda un InviteToGroup por cada uno.
  QStringList pendingGroupInvitees_;

  // Mensajes sin leer por conversacion -- equivalente movil de
  // unreadCounts_ en MainWindow.hpp. Se mantiene aparte de conversations_
  // (y no solo dentro del modelo) porque un grupo puede acumular no
  // leidos antes de que su MyGroups llegue y lo liste -- ver markUnread.
  std::unordered_map<std::string, int> unreadCounts_;

  // Invitaciones a grupo recibidas y todavia sin aceptar/rechazar -- cada
  // entrada es un QVariantMap {inviteId (quint32), groupName, inviter}.
  QVariantList pendingInvites_;

  // --- Transferencia de archivos -- mismos structs/limites/mecanismo que
  // OutgoingTransfer/PendingBlobDownload/ActiveBlobDownload en
  // MainWindow.hpp del escritorio (ver el comentario de sendFile para el
  // mecanismo completo). ---
  struct OutgoingTransfer {
    std::string peer;  // peer 1-a-1, o groupId si isGroup
    bool isGroup = false;
    QString filename;
    quint64 totalSize = 0;
    quint64 sentBytes = 0;
    std::unique_ptr<QFile> file;
    templar::crypto::FileKey key{};
    std::unique_ptr<templar::crypto::FileEncryptor> encryptor;
    // Vacio hasta que llega UploadBlobBeginOk -- sendNextFileChunk no
    // arranca antes de tenerlo.
    std::string blobId;
  };
  std::optional<OutgoingTransfer> outgoingTransfer_;

  // Un puntero a archivo recibido y todavia sin descargar -- desaparece
  // de aqui en cuanto se pulsa el enlace (startBlobDownload lo mueve a
  // activeDownload_).
  struct PendingBlobDownload {
    std::string originKey;
    std::string sender;
    QString filename;
    quint64 fileSize = 0;
    templar::crypto::FileKey key{};
    templar::crypto::FileHeader header{};
  };
  std::unordered_map<std::string, PendingBlobDownload> pendingBlobDownloads_;

  // La descarga en curso ahora mismo -- solo puede haber una a la vez.
  struct ActiveBlobDownload {
    std::string blobId;
    std::string originKey;
    std::string sender;
    QString filename;
    quint64 totalSize = 0;
    quint64 receivedBytes = 0;
    std::unique_ptr<QFile> file;
    std::unique_ptr<templar::crypto::FileDecryptor> decryptor;
    // true en cuanto un fragmento llega marcado como el ultimo de la
    // secuencia -- si BlobEnd llega sin que esto se haya puesto a true, el
    // archivo se corto en transito.
    bool finalTagSeen = false;
  };
  std::optional<ActiveBlobDownload> activeDownload_;

  QTimer* fileSendTimer_ = nullptr;
  QString fileTransferStatus_;
  double fileTransferProgress_ = 0.0;

  std::string myUsername_;
  // Recordados al llamar a registerAccount()/login(): las respuestas
  // RegisterOk/LoginOk no repiten el usuario/contrasena, asi que hace falta
  // tenerlos a mano para poder desbloquear LocalStore cuando lleguen (mismo
  // patron que pendingLoginPassword_ en MainWindow del escritorio).
  std::string pendingUsername_;
  std::string pendingPassword_;

  bool connected_ = false;
  bool loggedIn_ = false;
  QString statusText_ = QStringLiteral("Desconectado");
  QString errorText_;
  QString serverAddress_;
};

}  // namespace templar::phone
