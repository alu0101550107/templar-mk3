#pragma once

#include <sodium.h>

#include <QFile>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QWidget>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CryptoEngine.hpp"
#include "LocalStore.hpp"
#include "MessagePayload.hpp"
#include "NetworkManager.hpp"
#include "Theme.hpp"
#include "UpdateChecker.hpp"
#include "templar/crypto/FileCrypto.hpp"

class QLineEdit;
class QPushButton;
class QTextEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QMenu;
class QCloseEvent;
class QProgressBar;
class QTimer;

namespace templar::client {

class BackgroundWidget;

// Tipo de una linea de chat -- determina que color del Theme se usa al
// renderizarla. Los valores coinciden con lo que se guarda como `kind` en
// LocalStore (entero opaco para esa clase).
enum class LineKind { System = 0, Own = 1, Peer = 2 };

struct ChatLine {
  LineKind kind;
  QString who;
  QString text;
  // Epoch en segundos (UTC). 0 = desconocido (mensaje de antes de que el
  // historial guardara la hora) -- formatLine lo renderiza sin el prefijo
  // [HH:MM] en ese caso.
  qint64 timestamp = 0;
  // Casi siempre false: `text` se escapa como HTML antes de mostrarlo
  // (nunca hay que confiar en texto que puede venir de otra persona). Solo
  // se pone a true para la unica linea que SI generamos nosotros mismos
  // con HTML de proposito (el enlace "Descargar archivo" de un
  // FileBlobPointer, ver onFileBlobPointerReceived) -- no para nada que
  // venga de fuera.
  bool rawHtml = false;
  // Vacios si este mensaje no responde a otro -- si no, una COPIA
  // (recortada) del mensaje original, no una referencia por id (ver el
  // comentario de MessagePayload::TextReply para el porque).
  QString replyToSender;
  QString replyToText;
};

// Estado de un grupo, en memoria: se refresca por completo desde el
// servidor (autoridad de membresia) en cada login y cada vez que se acepta
// una invitacion nueva -- ver MsgType::MyGroups. No se persiste en
// LocalStore a proposito: el historial de mensajes de un grupo SI se
// guarda, reusando `history`/`unread_counts` con el id de grupo como clave
// de conversacion (igual que ya se hace con un peer), pero el propio
// nombre/admin/lista de miembros siempre se vuelve a pedir al servidor.
struct GroupInfo {
  std::string id;
  std::string name;
  std::string adminUsername;
  std::vector<std::string> members;
};

// Ventana única con dos pantallas dentro de un QStackedWidget:
//   - loginPage_: conectar al servidor + registrarse/iniciar sesión.
//   - chatPage_:  barra lateral de conversaciones + chat activo.
// Se pasa de una a otra tras un login exitoso, y "Desconectar" vuelve a la
// pantalla de login. El historial se guarda como datos estructurados (no
// HTML ya coloreado) para poder re-renderizarlo con el tema activo en cada
// momento, incluso si el usuario cambia los colores en Ajustes despues de
// que ya existan mensajes guardados.
class MainWindow : public QWidget {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow();

 protected:
  // Al cerrar la ventana (la X), en vez de terminar la app la ocultamos y
  // se queda viva en la bandeja del sistema -- la conexion con el servidor
  // sigue activa para poder seguir recibiendo mensajes y notificar.
  void closeEvent(QCloseEvent* event) override;

  // Detecta clicks en el enlace "Descargar" de un FileBlobPointer dentro
  // de chatView_ -- QTextEdit (a diferencia de QTextBrowser) no tiene una
  // senal anchorClicked propia, asi que hace falta interceptar el click a
  // mano sobre su viewport.
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void onConnectClicked();
  void onRegisterClicked();
  void onLoginClicked();
  void onDisconnectClicked();
  void onSendClicked();
  void onNewChatClicked();
  void onEmojiButtonClicked();
  // Inserta el emoji en messageEdit_ en la posicion del cursor -- separado
  // del popup real (EmojiPicker) para poder invocarlo directo desde un test
  // sin tener que interactuar con un Qt::Popup.
  void insertEmoji(const QString& emoji);
  void onAttachClicked();
  // Logica real de iniciar un envio, separada de onAttachClicked() (que solo
  // hace los chequeos previos y pregunta la ruta via QFileDialog) para poder
  // probarla directamente sin pelear con un dialogo modal en tests
  // automatizados sin pantalla real.
  void startOutgoingFileTransfer(const QString& path);
  void sendNextFileChunk();
  void onSettingsClicked();
  void onConversationSelected(QListWidgetItem* current, QListWidgetItem* previous);

  void onCreateGroupClicked();
  void onKickClicked();
  void onLeaveGroupClicked();
  void onAddMemberClicked();
  // Actuan sobre el item seleccionado (o el primero si no hay seleccion) de
  // inviteList_ -- ver respondToSelectedInvite.
  void onAcceptInviteClicked();
  void onRejectInviteClicked();

  void onSearchToggleClicked();
  void onSearchTextChanged(const QString& text);
  void onSearchNextClicked();
  void onSearchPrevClicked();

  void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
  void onTrayShowClicked();
  void onTrayQuitClicked();

  void onNetConnected();
  void onNetDisconnected();
  void onNetError(QString message);
  void onFrameReceived(templar::proto::MsgType type, templar::proto::Bytes payload);

  // Se dispara cuando updateChecker_ encuentra un release mas nuevo que
  // este binario (comprobacion en segundo plano, ver el constructor) --
  // solo muestra el banner, nunca interrumpe con un dialogo.
  void onUpdateAvailable();

 private:
  static constexpr const char* kSystemKey = "";

  QWidget* buildLoginPage();
  QWidget* buildChatPage();

  void logSystem(const QString& text);
  // persistText: si no esta vacio, es lo que se guarda en LocalStore EN VEZ
  // de `text` (que sigue siendo lo que se muestra en directo). Solo hace
  // falta cuando text es HTML crudo (rawHtml=true) cuyos enlaces ya no
  // funcionaran tras un reinicio (ver pendingBlobDownloads_, solo en
  // memoria) -- sin esto, el historial recargado mostraria el markup sin
  // escapar como texto suelto en vez de algo legible.
  // timestamp: 0 (por defecto) usa la hora actual -- se fija explicito solo
  // al procesar un DeliverMsg/DeliverGroupMsg, con la hora real de envio
  // que manda el servidor (campo sentAt), para que un mensaje recibido de
  // alguien desconectado muestre cuando se mando, no cuando se entrego.
  // replyToSender/replyToText: vacios si no responde a nada -- ver
  // ChatLine::replyToSender.
  void logChat(const std::string& peerKey, LineKind kind, const QString& who, const QString& text,
              bool rawHtml = false, const QString& persistText = QString(), qint64 timestamp = 0,
              const QString& replyToSender = QString(), const QString& replyToText = QString());
  void ensureConversationListed(const std::string& key, const QString& label);
  void renderActiveConversation();
  void selectConversation(const std::string& key);
  QListWidgetItem* findConversationItem(const std::string& key) const;
  void loadHistoryFromStore();

  // --- Busqueda dentro de la conversacion abierta ---
  // Solo busca en el chat activo (no global): usa QTextEdit::find(), que ya
  // resalta y hace scroll hasta la coincidencia. El contador de resultados
  // se recalcula aparte contando ocurrencias en el texto plano -- find() no
  // da esa cifra por si solo.
  void updateSearchMatchCount();
  void closeSearchBar();

  // Se llama al reconstruir chatView_ (cambio de conversacion, tema, o
  // llegada de un mensaje nuevo): cualquier busqueda activa queda invalida
  // porque el documento se reconstruye entero.
  void resetSearchState();

  // Marca/limpia "no leido" para una conversacion: actualiza el contador en
  // memoria, el estilo del item en la barra lateral (negrita + "(N)"), y lo
  // persiste en LocalStore para que sobreviva a cerrar la app.
  void markUnread(const std::string& peerKey);
  void clearUnread(const std::string& peerKey);
  void updateSidebarUnreadStyle(const std::string& peerKey);
  void loadUnreadCountsFromStore();

  // Punto de color para la barra lateral: verde si el peer esta conectado,
  // gris si no (o si todavia no sabemos -- estado por defecto hasta que
  // llegue la respuesta de la suscripcion).
  QIcon presenceIcon(bool online) const;

  // Envuelve localStore_.saveSession en un try/catch -- un fallo de
  // persistencia local NUNCA debe poder cortar el envio/recepcion de un
  // mensaje en vivo (p.ej. abortar antes de mandar el Ack al servidor).
  void trySaveSession(const std::string& peer);

  // Si el pool local de one-time prekeys esta bajo, genera mas hasta el
  // objetivo y publica TODAS las que haya en local (idempotente en el
  // servidor). Se llama tras cada login -- barato, y asegura que el pool
  // nunca se quede en cero aunque el cliente lleve mucho sin conectarse.
  static constexpr size_t kOneTimePrekeyLowWatermark = 5;
  static constexpr size_t kOneTimePrekeyTarget = 20;
  void ensureOneTimePrekeys();
  // Busca en LocalStore, por clave publica, la one-time prekey que el
  // remitente dice haber usado para el primer mensaje que acaba de llegar
  // -- y la CONSUME (se borra de LocalStore) si la encuentra. Vacio (y sin
  // tocar nada) si usedOneTimePrekeyPub esta vacio, o si por lo que sea no
  // se encuentra localmente (cae a X3DH en modo 3-DH, ver decryptFirst).
  std::optional<templar::crypto::X25519KeyPair> takeMatchingOneTimePrekey(
      const templar::proto::Bytes& usedOneTimePrekeyPub);

  // --- Grupos ---
  // Nombre a mostrar en la barra lateral para una clave de conversacion:
  // el nombre del grupo si `key` es un id de grupo conocido, o la propia
  // clave (username del peer) en caso contrario -- ver GroupInfo.
  QString displayLabelFor(const std::string& key) const;

  // Registra la linea "propia" una sola vez y arranca el reparto (fan-out):
  // un SendGroupMsg independiente por cada miembro, cifrado con el canal
  // 1-a-1 que ya exista (o se bootstree) con cada uno -- ver continueGroupFanout.
  void sendGroupMessage(const std::string& groupId, const std::string& text);
  // Procesa la cola de miembros pendientes de activeGroupFanout_ hasta
  // vaciarla o hasta toparse con uno que necesite bootstrap X3DH (FetchPrekeyBundle),
  // en cuyo caso se para a esperar la respuesta (ver case PrekeyBundle) y se
  // reanuda desde ahi.
  void continueGroupFanout();
  void sendGroupCiphertextToServer(const std::string& groupId, const std::string& recipient,
                                   const templar::proto::Bytes& ciphertext);

  // Refresca la etiqueta "Grupo: N miembro(s) -- admin: X" y la visibilidad
  // de los botones Expulsar/Anadir miembro/Salir para la conversacion activa.
  void updateGroupHeader();
  // Quita un grupo de la barra lateral (no borra su historial local): usado
  // tanto cuando me expulsan/salgo como al descubrir en un MyGroups fresco
  // que ya no pertenezco a un grupo que si conocia.
  void removeGroupFromSidebar(const std::string& groupId);

  // Anade una invitacion recien llegada al panel de la barra lateral (sin
  // ningun dialogo modal encima de lo que este haciendo el usuario en ese
  // momento -- se queda ahi hasta que el la mire y decida) y actualiza su
  // visibilidad.
  void onGroupInviteReceived(uint32_t inviteId, const std::string& groupName,
                             const std::string& inviter);
  // Manda Accept/RejectGroupInvite al servidor para el inviteId dado.
  void respondToGroupInvite(uint32_t inviteId, bool accept);
  // Resuelve la invitacion seleccionada en inviteList_ (o la primera si no
  // hay seleccion) y la quita del panel.
  void respondToSelectedInvite(bool accept);
  void updateInvitePanelVisibility();

  QString formatLine(const ChatLine& line) const;

  // --- Responder a un mensaje ---
  // Se activa al pulsar el icono ↩ de una linea (ver formatLine/eventFilter,
  // enlace "templar-reply:") -- muestra replyBarWidget_ con una vista previa
  // y deja el siguiente envio como respuesta a ese mensaje. Cambiar de
  // conversacion cancela cualquier respuesta pendiente (ver
  // onConversationSelected): no tiene sentido citar un mensaje de un chat
  // distinto al que se esta escribiendo.
  void startReply(const QString& sender, const QString& text);
  void cancelReply();
  // Codifica el texto tecleado como Text o TextReply segun haya o no una
  // respuesta pendiente -- centraliza esa decision para no repetirla en
  // cada sitio que manda un mensaje (1-a-1 directo, bootstrap X3DH, grupo).
  templar::proto::Bytes encodeOutgoingText(const std::string& text) const;
  // Separador de fecha ("6 de agosto de 2026") que se inserta entre dos
  // mensajes consecutivos cuyo dia local difiere -- se calcula al
  // renderizar a partir de ChatLine::timestamp, no se guarda como una
  // linea mas del historial (ni en memoria ni en LocalStore). Evita tener
  // que escribir la fecha "a las 00:00" (la app no esta corriendo 24/7) o
  // "al primer mensaje del dia" (mas estado que mantener, y no cubriria
  // bien un lote de mensajes atrasados que abarque varios dias a la vez).
  QString dateDividerHtml(qint64 epochSecs) const;
  static bool sameLocalDay(qint64 epochSecsA, qint64 epochSecsB);
  // Anade `line` a chatView_ si `key` es la conversacion activa ahora
  // mismo, insertando antes un separador de fecha si hace falta -- mismo
  // criterio de dia que renderActiveConversation(), factorizado aqui para
  // no duplicarlo entre logChat/logSystem.
  void appendLiveLine(const std::string& key, const ChatLine& line);
  void applyTheme();

  void setupTrayIcon();
  // true si la ventana no es la que tiene el foco ahora mismo (oculta en la
  // bandeja, minimizada, o simplemente el usuario esta en otra app) -- ese
  // es el criterio para disparar una notificacion nativa al llegar mensaje.
  bool shouldNotify() const;

  void setConnectedUiState(bool connected);
  void setLoggedInUiState(bool loggedIn);
  void setLoginError(const QString& message);

  templar::proto::Bytes buildRegisterPayload(const std::string& username,
                                             const std::string& password) const;

  // Arma y manda el frame SendMsg de bajo nivel (mismo formato de red para
  // texto normal y para cada paso de una transferencia de archivo -- el
  // servidor ve exactamente el mismo tipo de blob opaco en ambos casos).
  void sendEncryptedToServer(const std::string& peer, const templar::proto::Bytes& ciphertext);

  // --- Transferencia de archivos ---
  // El archivo se cifra UNA vez con una clave simetrica propia (ver
  // templar::crypto::FileCrypto -- nada que ver con ninguna sesion Double
  // Ratchet) y se sube UNA vez al servidor como blob opaco; lo unico que
  // viaja por el canal cifrado de cada destinatario es un puntero pequeno
  // (FileBlobPointer: id del blob + clave para abrirlo). Sustituye al
  // mecanismo anterior (trocear el archivo y mandar cada trozo por el
  // Double Ratchet de cada destinatario, un archivo entero por cada uno en
  // un grupo, e imposible con el destinatario desconectado) -- ver el
  // comentario de UploadBlobBegin en Protocol.hpp para el detalle completo.
  static constexpr uint64_t kMaxFileSize = 200ull * 1024 * 1024;
  static constexpr qint64 kFileChunkSize = 64 * 1024;

  void cancelActiveTransfers();
  QString uniqueDownloadPath(const QString& dir, const QString& filename) const;

  // Adjuntar un archivo en el chat CONTIGO MISMO (peer == myUsername_) no
  // pasa por el servidor en absoluto -- no hay a quien mas llegarle, asi
  // que no tiene sentido cifrar+subir+bajar. Se copia el archivo a una
  // carpeta local propia (para que sobreviva aunque el original se mueva o
  // se borre) y se deja un enlace que lo abre directo con la app del
  // sistema (ver eventFilter, prefijo "templar-selffile:").
  void startSelfFileAttach(const QString& path);

  // Arma el FileBlobPointer y lo manda (1-a-1 directo, o fan-out si es un
  // grupo) una vez el servidor confirma que la subida termino
  // (UploadBlobEndOk) -- ver sendNextFileChunk.
  void sendBlobPointerAndFinish();

  // Llegada de un puntero a archivo: `originKey` es el peer o groupId bajo
  // el que se registra en el historial (logChat), `sender` es quien lo
  // mando de verdad (puede diferir de originKey en un grupo). Guarda la
  // clave en pendingBlobDownloads_ y deja un enlace "Descargar" en el chat
  // -- no se descarga sola, el usuario decide cuando (ver
  // startBlobDownload).
  // sentAt: hora real de envio (campo sentAt de DeliverMsg/DeliverGroupMsg),
  // no la de recepcion -- ver el comentario de logChat.
  void onFileBlobPointerReceived(const std::string& originKey, const std::string& sender,
                                 const std::string& blobId, const QString& filename,
                                 uint64_t fileSize, const templar::proto::Bytes& fileKey,
                                 const templar::proto::Bytes& fileHeader, qint64 sentAt);
  // Se llama al pulsar un enlace "Descargar" (ver eventFilter). No hace
  // nada si blobId no esta en pendingBlobDownloads_ -- p.ej. si la app se
  // reinicio desde que llego el puntero (la clave solo vive en memoria, a
  // proposito: guardarla en LocalStore mereceria su propia migracion de
  // esquema, se deja para si hace falta de verdad).
  void startBlobDownload(const std::string& blobId);
  void onBlobDataReceived(const std::string& blobId, const templar::proto::Bytes& chunk);
  void onBlobEndReceived(const std::string& blobId);
  void onBlobNotFound(const std::string& blobId, const std::string& reason);

  struct OutgoingTransfer {
    std::string peer;  // peer 1-a-1, o groupId si isGroup
    bool isGroup = false;
    QString filename;
    uint64_t totalSize = 0;
    uint64_t sentBytes = 0;
    std::unique_ptr<QFile> file;
    templar::crypto::FileKey key{};
    std::unique_ptr<templar::crypto::FileEncryptor> encryptor;
    // Vacio hasta que llega UploadBlobBeginOk -- sendNextFileChunk no
    // arranca antes de tenerlo.
    std::string blobId;
  };
  std::optional<OutgoingTransfer> outgoingTransfer_;
  QTimer* fileSendTimer_ = nullptr;

  // Un puntero a archivo recibido y todavia sin descargar -- desaparece de
  // aqui en cuanto se pulsa el enlace (startBlobDownload lo mueve a
  // activeDownload_), asi que un id que ya no esta aqui significa "ya se
  // esta descargando o ya se descargo, no ambas cosas otra vez".
  struct PendingBlobDownload {
    std::string originKey;
    std::string sender;
    QString filename;
    uint64_t fileSize = 0;
    templar::crypto::FileKey key{};
    templar::crypto::FileHeader header{};
  };
  std::unordered_map<std::string, PendingBlobDownload> pendingBlobDownloads_;

  // La descarga en curso ahora mismo -- solo puede haber una a la vez,
  // mismo criterio que el antiguo incomingTransfer_.
  struct ActiveBlobDownload {
    std::string blobId;
    std::string originKey;
    std::string sender;
    QString filename;
    uint64_t totalSize = 0;
    uint64_t receivedBytes = 0;
    std::unique_ptr<QFile> file;
    std::unique_ptr<templar::crypto::FileDecryptor> decryptor;
    // true en cuanto un fragmento llega marcado como el ultimo de la
    // secuencia (ver FileDecryptor::Result::isLast) -- si BlobEnd llega sin
    // que esto se haya puesto a true, el archivo se corto en transito y no
    // hay que darlo por bueno aunque el servidor diga que ya termino.
    bool finalTagSeen = false;
  };
  std::optional<ActiveBlobDownload> activeDownload_;

  NetworkManager net_;
  CryptoEngine crypto_;
  LocalStore localStore_;
  Theme theme_;
  templar::UpdateChecker updateChecker_;
  std::string myUsername_;
  std::string pendingLoginPassword_;
  bool loggedIn_ = false;

  struct PendingOutbound {
    std::string peer;
    // Vacio para un mensaje 1-a-1 normal; si no, indica que este bootstrap
    // X3DH es un paso del fan-out de un mensaje de grupo, y hay que
    // continuar la cola (continueGroupFanout) al terminar.
    std::string groupId;
    // Payload YA CODIFICADO (ver MessagePayload::encode*) -- quien crea
    // este PendingOutbound decide que tipo de mensaje es (texto,
    // FileBlobPointer...), aqui solo se cifra y se manda tal cual.
    templar::proto::Bytes payload;
    // Que mostrar en el historial PROPIO cuando este envio 1-a-1 complete
    // (solo se usa si groupId esta vacio -- en un fan-out de grupo la
    // linea propia ya se registro una vez al arrancar el fan-out).
    QString ownLineText;
    // Igual que ownLineText: solo se usan si groupId esta vacio, para que
    // la linea propia que se registra al completar el bootstrap tambien
    // lleve la cita si el mensaje original era una respuesta.
    QString replyToSender;
    QString replyToText;
  };
  std::optional<PendingOutbound> pendingOutbound_;

  // --- Responder a un mensaje ---
  bool hasPendingReply_ = false;
  QString pendingReplyToSender_;
  QString pendingReplyToText_;

  std::string activeConversation_ = kSystemKey;
  std::unordered_map<std::string, std::vector<ChatLine>> conversations_;
  std::unordered_set<std::string> listedKeys_;
  std::unordered_map<std::string, int> unreadCounts_;

  // --- Grupos ---
  std::unordered_map<std::string, GroupInfo> groups_;
  // Ids de grupo que este dispositivo ha conocido alguna vez, cargado de
  // LocalStore justo antes de loadHistoryFromStore() -- a diferencia de
  // groups_ (que solo tiene los grupos ACTUALES, refrescado por completo en
  // cada MyGroups), este conjunto sigue teniendo un id incluso despues de
  // perder la membresia, para poder distinguir "conversation_key con
  // historial que es un grupo viejo" de "conversation_key que es un peer".
  std::unordered_set<std::string> knownGroupKeys_;
  // Contactos marcados en CreateGroupDialog, guardados entre mandar
  // CreateGroup y recibir GroupCreated (ver ese case) para invitarlos justo
  // despues de que el grupo exista de verdad.
  std::vector<std::string> pendingGroupInvitees_;
  struct GroupFanout {
    std::string groupId;
    // Payload ya codificado -- mismo criterio que PendingOutbound::payload.
    templar::proto::Bytes payload;
    std::vector<std::string> remainingMembers;
  };
  std::optional<GroupFanout> activeGroupFanout_;

  // --- Navegacion ---
  QStackedWidget* stack_;
  QWidget* loginPage_;
  QWidget* chatPage_;
  BackgroundWidget* loginBackground_;
  BackgroundWidget* chatBackground_;

  // --- Pantalla de login ---
  QLineEdit* hostEdit_;
  QLineEdit* portEdit_;
  QPushButton* connectButton_;

  QLineEdit* usernameEdit_;
  QLineEdit* passwordEdit_;
  QPushButton* registerButton_;
  QPushButton* loginButton_;
  QLabel* loginErrorLabel_;

  // --- Pantalla de chat ---
  QLabel* connectedAsLabel_;
  QPushButton* disconnectButton_;
  QPushButton* searchToggleButton_;

  QListWidget* conversationList_;
  QLineEdit* newChatPeerEdit_;
  QPushButton* newChatButton_;
  QPushButton* createGroupButton_;

  // Panel de invitaciones a grupo pendientes: vive en la barra lateral, no
  // modal, visible solo mientras haya alguna -- ver onGroupInviteReceived.
  QLabel* inviteLabel_;
  QListWidget* inviteList_;
  QPushButton* acceptInviteButton_;
  QPushButton* rejectInviteButton_;

  QTextEdit* chatView_;

  // Barra de busqueda: oculta por defecto, la abre/cierra searchToggleButton_.
  // Solo busca dentro de conversations_[activeConversation_] / chatView_.
  QWidget* searchBarWidget_;
  QLineEdit* searchEdit_;
  QPushButton* searchPrevButton_;
  QPushButton* searchNextButton_;
  QLabel* searchCountLabel_;
  QPushButton* searchCloseButton_;

  QLabel* groupInfoLabel_;
  QPushButton* addMemberButton_;
  QPushButton* kickButton_;
  QPushButton* leaveGroupButton_;

  // Barra "Respondiendo a...": oculta por defecto, mismo criterio que
  // searchBarWidget_ -- aparece encima de la fila de envio al pulsar ↩ en
  // un mensaje (ver startReply).
  QWidget* replyBarWidget_;
  QLabel* replyBarLabel_;
  QPushButton* replyBarCloseButton_;

  QLineEdit* messageEdit_;
  QPushButton* sendButton_;
  QPushButton* emojiButton_;
  QPushButton* attachButton_;
  QProgressBar* transferProgress_;
  QLabel* transferLabel_;

  QLabel* statusLabel_;
  // Oculto salvo que updateChecker_ encuentre una version mas nueva -- ver
  // onUpdateAvailable(). Vive en la fila inferior (fuera del
  // QStackedWidget), visible tanto en login como en chat, mismo criterio
  // que statusLabel_/closeButton_.
  QLabel* updateBanner_;
  QPushButton* settingsButton_;
  // Visible en login Y en chat (vive fuera del QStackedWidget, en la fila
  // inferior): para quien no tenga un boton de cerrar en la decoracion de
  // su gestor de ventanas (o no sepa el atajo de teclado). Hace exactamente
  // lo mismo que la X nativa de la ventana -- ver MainWindow::closeEvent.
  QPushButton* closeButton_;

  // --- Bandeja del sistema ---
  QSystemTrayIcon* trayIcon_ = nullptr;
  QMenu* trayMenu_ = nullptr;
  bool trayHintShown_ = false;
};

}  // namespace templar::client
