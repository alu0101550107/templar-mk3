#include "MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "BackgroundWidget.hpp"
#include "CreateGroupDialog.hpp"
#include "EmojiPicker.hpp"
#include "Language.hpp"
#include "SettingsDialog.hpp"
#include "templar/Wire.hpp"

namespace {
const QPixmap& logoPixmap() {
  static const QPixmap pixmap(":/great_helmet_logo.png");
  return pixmap;
}
}  // namespace

namespace templar::client {

using namespace templar::proto;
using templar::crypto::PrekeyBundle;

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
  setWindowTitle("Templar mk3");  // nombre de marca, no se traduce
  resize(800, 560);

  stack_ = new QStackedWidget();
  loginPage_ = buildLoginPage();
  chatPage_ = buildChatPage();
  stack_->addWidget(loginPage_);
  stack_->addWidget(chatPage_);
  stack_->setCurrentWidget(loginPage_);

  statusLabel_ = new QLabel(tr("Desconectado."));
  settingsButton_ = new QPushButton(tr("⚙ Ajustes"));
  closeButton_ = new QPushButton("✕");
  closeButton_->setObjectName("closeButton");
  closeButton_->setToolTip(tr("Cerrar (sigue en la bandeja del sistema)"));

  auto* bottomRow = new QHBoxLayout();
  bottomRow->addWidget(statusLabel_, /*stretch=*/1);
  bottomRow->addWidget(settingsButton_);
  bottomRow->addWidget(closeButton_);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(stack_, /*stretch=*/1);
  layout->addLayout(bottomRow);

  // El log de sistema es una "conversacion" mas en la barra lateral, pero
  // fija y separada de los chats reales -- asi los avisos de conexion nunca
  // se mezclan con mensajes de un peer.
  ensureConversationListed(kSystemKey, tr("Sistema"));
  conversationList_->setCurrentRow(0);

  setConnectedUiState(false);
  setLoggedInUiState(false);

  connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
  connect(registerButton_, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
  connect(loginButton_, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
  connect(disconnectButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
  connect(messageEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
  connect(newChatButton_, &QPushButton::clicked, this, &MainWindow::onNewChatClicked);
  connect(newChatPeerEdit_, &QLineEdit::returnPressed, this, &MainWindow::onNewChatClicked);
  connect(createGroupButton_, &QPushButton::clicked, this, &MainWindow::onCreateGroupClicked);
  connect(kickButton_, &QPushButton::clicked, this, &MainWindow::onKickClicked);
  connect(leaveGroupButton_, &QPushButton::clicked, this, &MainWindow::onLeaveGroupClicked);
  connect(addMemberButton_, &QPushButton::clicked, this, &MainWindow::onAddMemberClicked);
  connect(acceptInviteButton_, &QPushButton::clicked, this, &MainWindow::onAcceptInviteClicked);
  connect(rejectInviteButton_, &QPushButton::clicked, this, &MainWindow::onRejectInviteClicked);
  connect(emojiButton_, &QPushButton::clicked, this, &MainWindow::onEmojiButtonClicked);
  connect(attachButton_, &QPushButton::clicked, this, &MainWindow::onAttachClicked);
  connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
  // close() dispara un QCloseEvent normal -- pasa por closeEvent() como si
  // se hubiera pulsado la X nativa de la ventana, sin duplicar esa logica.
  connect(closeButton_, &QPushButton::clicked, this, &QWidget::close);
  connect(searchToggleButton_, &QPushButton::clicked, this, &MainWindow::onSearchToggleClicked);
  connect(searchCloseButton_, &QPushButton::clicked, this, &MainWindow::closeSearchBar);
  connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
  connect(searchEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSearchNextClicked);
  connect(searchNextButton_, &QPushButton::clicked, this, &MainWindow::onSearchNextClicked);
  connect(searchPrevButton_, &QPushButton::clicked, this, &MainWindow::onSearchPrevClicked);
  connect(conversationList_, &QListWidget::currentItemChanged, this,
         &MainWindow::onConversationSelected);

  connect(&net_, &NetworkManager::connected, this, &MainWindow::onNetConnected);
  connect(&net_, &NetworkManager::disconnected, this, &MainWindow::onNetDisconnected);
  connect(&net_, &NetworkManager::connectionError, this, &MainWindow::onNetError);
  connect(&net_, &NetworkManager::frameReceived, this, &MainWindow::onFrameReceived);

  fileSendTimer_ = new QTimer(this);
  connect(fileSendTimer_, &QTimer::timeout, this, &MainWindow::sendNextFileChunk);

  theme_ = Theme::load();
  applyTheme();

  setupTrayIcon();
}

MainWindow::~MainWindow() {
  // Los miembros se destruyen en orden inverso al de declaracion: net_ (el
  // primero declarado) se destruye EL ULTIMO. Al destruirse, el QTcpSocket
  // interno dispara disconnectFromHost(), que emite disconnected() de forma
  // sincrona -- y ese slot (onNetDisconnected) toca conversations_ y otros
  // miembros que para entonces YA se destruyeron, un use-after-free real
  // que corrompe el heap. Desconectar todas las senales de net_ antes de
  // que empiece la destruccion evita que eso pueda pasar.
  net_.disconnect();
}

// --- Bandeja del sistema ---

void MainWindow::setupTrayIcon() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    // Sin bandeja disponible (algunos entornos minimalistas) -- cerrar la
    // ventana cierra la app de verdad, como haria cualquier programa normal.
    return;
  }

  trayMenu_ = new QMenu(this);
  QAction* showAction = trayMenu_->addAction(tr("Abrir Templar"));
  QAction* quitAction = trayMenu_->addAction(tr("Salir"));
  connect(showAction, &QAction::triggered, this, &MainWindow::onTrayShowClicked);
  connect(quitAction, &QAction::triggered, this, &MainWindow::onTrayQuitClicked);

  trayIcon_ = new QSystemTrayIcon(QIcon(logoPixmap()), this);
  trayIcon_->setToolTip("Templar");  // nombre de marca, no se traduce
  trayIcon_->setContextMenu(trayMenu_);
  connect(trayIcon_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
  trayIcon_->show();
}

bool MainWindow::shouldNotify() const { return isHidden() || !isActiveWindow(); }

void MainWindow::closeEvent(QCloseEvent* event) {
  if (!trayIcon_) {
    event->accept();  // sin bandeja disponible: cerrar de verdad
    return;
  }

  hide();
  event->ignore();

  if (!trayHintShown_) {
    trayHintShown_ = true;
    trayIcon_->showMessage(
        tr("Templar sigue activo"),
        tr("Sigue conectado en segundo plano. Haz clic aqui para volver a abrirlo, o usa "
           "'Salir' en el menu de la bandeja para cerrarlo del todo."),
        QSystemTrayIcon::Information, 5000);
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == chatView_->viewport() && event->type() == QEvent::MouseButtonRelease) {
    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      QString anchor = chatView_->anchorAt(mouseEvent->pos());
      static const QString kDownloadPrefix = "templar-download:";
      static const QString kSelfFilePrefix = "templar-selffile:";
      if (anchor.startsWith(kDownloadPrefix)) {
        startBlobDownload(anchor.mid(kDownloadPrefix.size()).toStdString());
        return true;  // consumido: no dejar que QTextEdit intente seleccionar texto
      }
      if (anchor.startsWith(kSelfFilePrefix)) {
        QString path =
            QUrl::fromPercentEncoding(anchor.mid(kSelfFilePrefix.size()).toUtf8());
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
          logSystem(tr("No se pudo abrir el archivo: ") + path);
        }
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
  if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
    onTrayShowClicked();
  }
}

void MainWindow::onTrayShowClicked() {
  show();
  raise();
  activateWindow();
}

void MainWindow::onTrayQuitClicked() { qApp->quit(); }

// --- Construccion de pantallas ---

QWidget* MainWindow::buildLoginPage() {
  hostEdit_ = new QLineEdit("127.0.0.1");
  hostEdit_->setObjectName("hostEdit");
  portEdit_ = new QLineEdit("8080");
  portEdit_->setObjectName("portEdit");
  connectButton_ = new QPushButton(tr("Conectar"));
  connectButton_->setObjectName("connectButton");

  auto* connRow = new QHBoxLayout();
  connRow->addWidget(new QLabel(tr("Servidor:")));
  connRow->addWidget(hostEdit_);
  connRow->addWidget(new QLabel(tr("Puerto:")));
  connRow->addWidget(portEdit_);
  connRow->addWidget(connectButton_);

  usernameEdit_ = new QLineEdit();
  usernameEdit_->setObjectName("usernameEdit");
  usernameEdit_->setPlaceholderText(tr("usuario"));
  passwordEdit_ = new QLineEdit();
  passwordEdit_->setObjectName("passwordEdit");
  passwordEdit_->setPlaceholderText(tr("contrasena (min. 8 caracteres)"));
  passwordEdit_->setEchoMode(QLineEdit::Password);
  registerButton_ = new QPushButton(tr("Registrarse"));
  registerButton_->setObjectName("registerButton");
  loginButton_ = new QPushButton(tr("Iniciar sesion"));
  loginButton_->setObjectName("loginButton");

  auto* authRow = new QHBoxLayout();
  authRow->addWidget(usernameEdit_);
  authRow->addWidget(passwordEdit_);
  authRow->addWidget(registerButton_);
  authRow->addWidget(loginButton_);

  loginErrorLabel_ = new QLabel();
  loginErrorLabel_->setStyleSheet("color: #ff6b6b; font-weight: bold;");
  loginErrorLabel_->setWordWrap(true);
  loginErrorLabel_->setAlignment(Qt::AlignCenter);
  loginErrorLabel_->setVisible(false);

  loginBackground_ = new BackgroundWidget();
  loginBackground_->setBackgroundPixmap(logoPixmap(), /*blurRadius=*/20.0, /*opacity=*/0.28);

  auto* pageLayout = new QVBoxLayout(loginBackground_);
  pageLayout->addStretch(1);
  pageLayout->addLayout(connRow);
  pageLayout->addLayout(authRow);
  pageLayout->addWidget(loginErrorLabel_);
  pageLayout->addStretch(2);
  return loginBackground_;
}

QWidget* MainWindow::buildChatPage() {
  connectedAsLabel_ = new QLabel();
  disconnectButton_ = new QPushButton(tr("Desconectar"));
  disconnectButton_->setObjectName("disconnectButton");
  searchToggleButton_ = new QPushButton("🔎");
  searchToggleButton_->setObjectName("searchToggleButton");
  searchToggleButton_->setToolTip(tr("Buscar en esta conversacion"));

  auto* topRow = new QHBoxLayout();
  topRow->addWidget(connectedAsLabel_, /*stretch=*/1);
  topRow->addWidget(searchToggleButton_);
  topRow->addWidget(disconnectButton_);

  conversationList_ = new QListWidget();
  conversationList_->setObjectName("conversationList");
  newChatPeerEdit_ = new QLineEdit();
  newChatPeerEdit_->setObjectName("newChatPeerEdit");
  newChatPeerEdit_->setPlaceholderText(tr("usuario..."));
  newChatButton_ = new QPushButton(tr("+ Nuevo chat"));
  newChatButton_->setObjectName("newChatButton");

  auto* newChatRow = new QHBoxLayout();
  newChatRow->addWidget(newChatPeerEdit_);
  newChatRow->addWidget(newChatButton_);

  createGroupButton_ = new QPushButton(tr("+ Nuevo grupo"));
  createGroupButton_->setObjectName("createGroupButton");

  // Panel de invitaciones pendientes: nada de dialogo modal que pueda
  // interrumpir al usuario en mitad de otra cosa -- se queda aqui, en la
  // propia ventana, hasta que decida mirarlo. Oculto por defecto, solo se
  // muestra mientras inviteList_ tenga algo (ver updateInvitePanelVisibility).
  inviteLabel_ = new QLabel(tr("Invitaciones pendientes"));
  inviteLabel_->setObjectName("inviteLabel");
  inviteLabel_->setVisible(false);
  inviteList_ = new QListWidget();
  inviteList_->setObjectName("inviteList");
  inviteList_->setMaximumHeight(80);
  inviteList_->setVisible(false);
  acceptInviteButton_ = new QPushButton(tr("Aceptar"));
  acceptInviteButton_->setObjectName("acceptInviteButton");
  acceptInviteButton_->setVisible(false);
  rejectInviteButton_ = new QPushButton(tr("Rechazar"));
  rejectInviteButton_->setObjectName("rejectInviteButton");
  rejectInviteButton_->setVisible(false);

  auto* inviteButtonsRow = new QHBoxLayout();
  inviteButtonsRow->addWidget(acceptInviteButton_);
  inviteButtonsRow->addWidget(rejectInviteButton_);

  auto* sidebarLayout = new QVBoxLayout();
  sidebarLayout->addWidget(new QLabel(tr("Conversaciones")));
  sidebarLayout->addWidget(conversationList_, /*stretch=*/1);
  sidebarLayout->addLayout(newChatRow);
  sidebarLayout->addWidget(createGroupButton_);
  sidebarLayout->addWidget(inviteLabel_);
  sidebarLayout->addWidget(inviteList_);
  sidebarLayout->addLayout(inviteButtonsRow);
  auto* sidebarWidget = new QWidget();
  sidebarWidget->setLayout(sidebarLayout);

  chatView_ = new QTextEdit();
  chatView_->setObjectName("chatView");
  chatView_->setReadOnly(true);
  chatView_->setFrameStyle(QFrame::NoFrame);
  chatView_->viewport()->setAutoFillBackground(false);
  // QTextEdit (a diferencia de QTextBrowser) no tiene una senal
  // anchorClicked propia -- se detecta el click a mano en eventFilter().
  chatView_->viewport()->installEventFilter(this);

  searchEdit_ = new QLineEdit();
  searchEdit_->setObjectName("searchEdit");
  searchEdit_->setPlaceholderText(tr("Buscar en esta conversacion..."));
  searchPrevButton_ = new QPushButton("↑");
  searchPrevButton_->setObjectName("searchPrevButton");
  searchPrevButton_->setToolTip(tr("Coincidencia anterior"));
  searchNextButton_ = new QPushButton("↓");
  searchNextButton_->setObjectName("searchNextButton");
  searchNextButton_->setToolTip(tr("Coincidencia siguiente"));
  searchCountLabel_ = new QLabel();
  searchCountLabel_->setObjectName("searchCountLabel");
  searchCloseButton_ = new QPushButton("✕");
  searchCloseButton_->setObjectName("searchCloseButton");

  auto* searchRow = new QHBoxLayout();
  searchRow->addWidget(searchEdit_, /*stretch=*/1);
  searchRow->addWidget(searchCountLabel_);
  searchRow->addWidget(searchPrevButton_);
  searchRow->addWidget(searchNextButton_);
  searchRow->addWidget(searchCloseButton_);
  searchBarWidget_ = new QWidget();
  searchBarWidget_->setObjectName("searchBarWidget");
  searchBarWidget_->setLayout(searchRow);
  searchBarWidget_->setVisible(false);

  groupInfoLabel_ = new QLabel();
  groupInfoLabel_->setObjectName("groupInfoLabel");
  groupInfoLabel_->setVisible(false);
  addMemberButton_ = new QPushButton(tr("Anadir miembro..."));
  addMemberButton_->setObjectName("addMemberButton");
  addMemberButton_->setVisible(false);
  kickButton_ = new QPushButton(tr("Expulsar..."));
  kickButton_->setObjectName("kickButton");
  kickButton_->setVisible(false);
  leaveGroupButton_ = new QPushButton(tr("Salir del grupo"));
  leaveGroupButton_->setObjectName("leaveGroupButton");
  leaveGroupButton_->setVisible(false);

  auto* groupHeaderRow = new QHBoxLayout();
  groupHeaderRow->addWidget(groupInfoLabel_, /*stretch=*/1);
  groupHeaderRow->addWidget(addMemberButton_);
  groupHeaderRow->addWidget(kickButton_);
  groupHeaderRow->addWidget(leaveGroupButton_);

  messageEdit_ = new QLineEdit();
  messageEdit_->setObjectName("messageEdit");
  messageEdit_->setPlaceholderText(tr("mensaje..."));
  emojiButton_ = new QPushButton("😀");
  emojiButton_->setObjectName("emojiButton");
  emojiButton_->setToolTip(tr("Insertar emoji"));
  attachButton_ = new QPushButton("📎");
  attachButton_->setObjectName("attachButton");
  attachButton_->setToolTip(tr("Enviar archivo"));
  sendButton_ = new QPushButton(tr("Enviar"));
  sendButton_->setObjectName("sendButton");

  auto* sendRow = new QHBoxLayout();
  sendRow->addWidget(messageEdit_, /*stretch=*/1);
  sendRow->addWidget(emojiButton_);
  sendRow->addWidget(attachButton_);
  sendRow->addWidget(sendButton_);

  transferLabel_ = new QLabel();
  transferLabel_->setObjectName("transferLabel");
  transferLabel_->setVisible(false);
  transferProgress_ = new QProgressBar();
  transferProgress_->setObjectName("transferProgress");
  transferProgress_->setRange(0, 100);
  transferProgress_->setVisible(false);

  auto* transferRow = new QHBoxLayout();
  transferRow->addWidget(transferLabel_);
  transferRow->addWidget(transferProgress_, /*stretch=*/1);

  auto* chatLayout = new QVBoxLayout();
  chatLayout->addLayout(groupHeaderRow);
  chatLayout->addWidget(searchBarWidget_);
  chatLayout->addWidget(chatView_, /*stretch=*/1);
  chatLayout->addLayout(transferRow);
  chatLayout->addLayout(sendRow);
  chatBackground_ = new BackgroundWidget();
  chatBackground_->setBackgroundPixmap(logoPixmap(), /*blurRadius=*/24.0, /*opacity=*/0.12);
  chatBackground_->setLayout(chatLayout);

  auto* splitter = new QSplitter();
  splitter->addWidget(sidebarWidget);
  splitter->addWidget(chatBackground_);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({200, 600});

  auto* page = new QWidget();
  auto* pageLayout = new QVBoxLayout(page);
  pageLayout->addLayout(topRow);
  pageLayout->addWidget(splitter, /*stretch=*/1);
  return page;
}

// --- Tema visual ---

QString MainWindow::formatLine(const ChatLine& line) const {
  // timestamp == 0 significa "desconocido" (mensaje guardado antes de que el
  // historial tuviera hora, ver LocalStore::migrateSchema) -- se omite el
  // prefijo en vez de inventarse una hora falsa.
  QString timePrefix;
  if (line.timestamp > 0) {
    QString hhmm = QDateTime::fromSecsSinceEpoch(line.timestamp).toString("HH:mm");
    timePrefix = "<span style='color:" + theme_.systemMessage.name() + ";'>[" + hhmm + "] </span>";
  }

  // Preserva saltos de linea reales (p.ej. de un mensaje pegado con varias
  // lineas) como <br> en vez de dejar que el HTML los colapse en un espacio.
  // Excepto para rawHtml=true (el enlace "Descargar" de un
  // FileBlobPointer): ese HTML lo generamos nosotros mismos a proposito,
  // escaparlo lo dejaria inutil.
  QString body = line.rawHtml ? line.text : line.text.toHtmlEscaped();
  if (!line.rawHtml) body.replace(QLatin1String("\n"), QLatin1String("<br>"));

  QString leftCell;
  QString rightCell;

  switch (line.kind) {
    case LineKind::System:
      leftCell = timePrefix;
      rightCell =
          "<i style='color:" + theme_.systemMessage.name() + ";'>[" + tr("SISTEMA") + "] " + body + "</i>";
      break;
    case LineKind::Own:
      leftCell = timePrefix + "<b style='color:" + theme_.ownMessage.name() + ";'>" +
                line.who.toHtmlEscaped() + ":</b>";
      rightCell = body;
      break;
    case LineKind::Peer:
      leftCell = timePrefix + "<b style='color:" + theme_.peerMessage.name() + ";'>" +
                line.who.toHtmlEscaped() + ":</b>";
      rightCell = body;
      break;
  }

  // Tabla de 2 columnas en vez de una sola linea de texto: si el mensaje
  // ocupa varias lineas (por ser largo y envolver, o por saltos de linea
  // reales), el contenido queda alineado bajo si mismo en la columna
  // derecha en vez de irse al margen izquierdo como si fuera un mensaje
  // nuevo. La columna izquierda (hora + nombre) no se envuelve nunca.
  return "<table style='margin:0;' cellspacing='0' cellpadding='0'><tr>"
        "<td style='white-space:nowrap; vertical-align:top; padding-right:6px;'>" + leftCell +
        "</td><td style='vertical-align:top;'>" + rightCell + "</td></tr></table>";
}

bool MainWindow::sameLocalDay(qint64 epochSecsA, qint64 epochSecsB) {
  return QDateTime::fromSecsSinceEpoch(epochSecsA).date() ==
        QDateTime::fromSecsSinceEpoch(epochSecsB).date();
}

QString MainWindow::dateDividerHtml(qint64 epochSecs) const {
  QDate date = QDateTime::fromSecsSinceEpoch(epochSecs).date();
  // El patron de fecha en si tambien depende del idioma (el espanol usa
  // "d 'de' MMMM 'de' yyyy"; el ingles no tiene ese "de" -- se deja que
  // QLocale::dateFormat() elija el patron correcto para cada idioma en vez
  // de fijar uno solo).
  QLocale locale = languageLocale(currentLanguage());
  QString text = locale.toString(date, locale.dateFormat(QLocale::LongFormat));
  return "<div style='text-align:center; margin:6px 0; color:" + theme_.systemMessage.name() +
        ";'><i>" + text.toHtmlEscaped() + "</i></div>";
}

void MainWindow::appendLiveLine(const std::string& key, const ChatLine& line) {
  if (activeConversation_ != key) return;
  const auto& history = conversations_[key];

  bool needDivider = line.timestamp > 0;
  if (needDivider && history.size() >= 2) {
    const ChatLine& prev = history[history.size() - 2];
    if (prev.timestamp > 0 && sameLocalDay(prev.timestamp, line.timestamp)) needDivider = false;
  }
  if (needDivider) chatView_->append(dateDividerHtml(line.timestamp));
  chatView_->append(formatLine(line));
}

void MainWindow::applyTheme() {
  qApp->setStyleSheet(theme_.toQss());

  if (loginBackground_) loginBackground_->setBaseColor(theme_.background);
  if (chatBackground_) chatBackground_->setBaseColor(theme_.background);

  if (chatView_) {
    // El fondo se mantiene transparente a proposito (para ver el logo
    // difuminado del contenedor); solo el color de texto sigue al tema.
    chatView_->setStyleSheet(
        QString("QTextEdit { background: transparent; color: %1; }").arg(theme_.foreground.name()));
  }

  renderActiveConversation();
}

void MainWindow::onSettingsClicked() {
  SettingsDialog dialog(theme_, this);
  if (dialog.exec() == QDialog::Accepted) {
    theme_ = dialog.resultTheme();
    theme_.save();
    applyTheme();

    Language chosenLanguage = dialog.resultLanguage();
    if (chosenLanguage != currentLanguage()) {
      setLanguage(chosenLanguage);
      logSystem(tr("Idioma cambiado. Reinicia Templar para que se aplique."));
    }
  }
}

// --- Gestion de conversaciones ---

QIcon MainWindow::presenceIcon(bool online) const {
  QPixmap pixmap(12, 12);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(online ? QColor("#2ecc71") : QColor("#777777"));
  painter.drawEllipse(1, 1, 10, 10);

  return QIcon(pixmap);
}

void MainWindow::ensureConversationListed(const std::string& key, const QString& label) {
  if (listedKeys_.count(key)) return;
  listedKeys_.insert(key);
  conversations_[key];  // crea el historial vacio si no existia

  bool isGroup = groups_.count(key) != 0;
  // "Sistema" y el chat contigo mismo no son peers de verdad -- ninguno de
  // los dos tiene un estado online/offline que suscribir ni mostrar (ver
  // el comentario de startSelfFileAttach para por que el chat contigo
  // mismo tampoco toca la red en absoluto).
  bool isPseudo = (key == kSystemKey || key == myUsername_);

  auto* item = new QListWidgetItem(label);
  item->setData(Qt::UserRole, QString::fromStdString(key));
  if (!isPseudo && !isGroup) {
    // Gris por defecto hasta que llegue la respuesta de la suscripcion.
    item->setIcon(presenceIcon(false));
  }
  conversationList_->addItem(item);

  if (!isPseudo && !isGroup && loggedIn_) {
    Writer w;
    w.str(key);
    net_.sendFrame(MsgType::SubscribePresence, w.take());
  }
}

void MainWindow::logSystem(const QString& text) {
  ChatLine line{LineKind::System, QString(), text, QDateTime::currentSecsSinceEpoch()};
  conversations_[kSystemKey].push_back(line);
  appendLiveLine(kSystemKey, line);
  if (localStore_.isUnlocked()) {
    try {
      localStore_.appendHistoryLine(kSystemKey, static_cast<int>(LineKind::System), "",
                                    text.toStdString());
    } catch (const std::exception& e) {
      // No usar logSystem() aqui (recursion) -- un fallo al persistir un
      // mensaje de log no debe impedir que el propio mensaje se muestre.
      qWarning() << "LocalStore: fallo guardando linea de sistema:" << e.what();
    }
  }
}

void MainWindow::logChat(const std::string& peerKey, LineKind kind, const QString& who,
                         const QString& text, bool rawHtml, const QString& persistText) {
  ensureConversationListed(peerKey, QString::fromStdString(peerKey));
  ChatLine line{kind, who, text, QDateTime::currentSecsSinceEpoch(), rawHtml};
  conversations_[peerKey].push_back(line);
  appendLiveLine(peerKey, line);
  if (localStore_.isUnlocked()) {
    try {
      const QString& toPersist = persistText.isEmpty() ? text : persistText;
      localStore_.appendHistoryLine(peerKey, static_cast<int>(kind), who.toStdString(),
                                    toPersist.toStdString(), rawHtml);
    } catch (const std::exception& e) {
      // Un fallo de persistencia local NUNCA debe poder cortar el flujo de
      // mensajeria en vivo (el mensaje ya se mostro arriba, ya se envio o
      // ya se descifro -- solo falla guardarlo para la proxima vez).
      qWarning() << "LocalStore: fallo guardando mensaje de" << QString::fromStdString(peerKey)
                 << ":" << e.what();
    }
  }
}

void MainWindow::trySaveSession(const std::string& peer) {
  if (!localStore_.isUnlocked()) return;
  try {
    localStore_.saveSession(peer, crypto_.exportSession(peer));
  } catch (const std::exception& e) {
    qWarning() << "LocalStore: fallo guardando la sesion con" << QString::fromStdString(peer)
               << ":" << e.what();
  }
}

void MainWindow::ensureOneTimePrekeys() {
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

    // Se republica el pool local completo, no solo lo nuevo: el servidor
    // ignora silenciosamente las que ya tenia (misma clave publica), y asi
    // no hace falta llevar la cuenta de cuales se subieron ya en una sesion
    // anterior.
    std::vector<Bytes> publics = localStore_.listOneTimePrekeyPublics();
    Writer w;
    w.u32(static_cast<uint32_t>(publics.size()));
    for (const Bytes& pub : publics) w.blob(pub);
    net_.sendFrame(MsgType::PublishOtpk, w.take());
  } catch (const std::exception& e) {
    qWarning() << "LocalStore: fallo gestionando el pool de one-time prekeys:" << e.what();
  }
}

std::optional<templar::crypto::X25519KeyPair> MainWindow::takeMatchingOneTimePrekey(
    const Bytes& usedOneTimePrekeyPub) {
  if (usedOneTimePrekeyPub.empty() || !localStore_.isUnlocked()) return std::nullopt;

  try {
    auto secret = localStore_.takeOneTimePrekeySecret(usedOneTimePrekeyPub);
    if (!secret || secret->size() != crypto_box_SECRETKEYBYTES ||
        usedOneTimePrekeyPub.size() != crypto_box_PUBLICKEYBYTES) {
      logSystem(
          tr("Aviso: el remitente dice haber usado una one-time prekey que no tenemos en local "
             "(almacen desincronizado) -- se cae a modo de 3-DH para este mensaje."));
      return std::nullopt;
    }
    templar::crypto::X25519KeyPair kp{};
    std::copy(usedOneTimePrekeyPub.begin(), usedOneTimePrekeyPub.end(), kp.pk);
    std::copy(secret->begin(), secret->end(), kp.sk);
    return kp;
  } catch (const std::exception& e) {
    qWarning() << "LocalStore: fallo consumiendo one-time prekey:" << e.what();
    return std::nullopt;
  }
}

void MainWindow::loadHistoryFromStore() {
  // El log de sistema puede ya tener lineas de ESTA sesion (p.ej. "Conectado
  // a ...", registradas antes del login) -- las cargadas del almacen van
  // primero para no romper el orden cronologico.
  std::vector<ChatLine> priorSystem;
  for (const auto& hl : localStore_.loadHistory(kSystemKey)) {
    priorSystem.push_back(
        ChatLine{static_cast<LineKind>(hl.kind), QString::fromStdString(hl.who),
                QString::fromStdString(hl.text), hl.createdAt, hl.rawHtml});
  }
  auto& sys = conversations_[kSystemKey];
  sys.insert(sys.begin(), priorSystem.begin(), priorSystem.end());

  for (const std::string& key : localStore_.listConversationKeys()) {
    if (key == kSystemKey) continue;

    // Un grupo (actual o viejo) NUNCA se lista aqui con la clave en crudo:
    // si todavia soy miembro, el handler de MyGroups (que llega en un frame
    // aparte, justo despues de LoginOk) lo listara el con su nombre real y
    // sin punto de presencia en cuanto llegue; si ya no soy miembro
    // (expulsado/sali/se borro mientras estaba desconectado), no vuelve a
    // listarse en absoluto -- pero su historial se sigue cargando abajo por
    // si alguna vez hace falta. Listarlo aqui de entrada mostraria el id
    // opaco como si fuera un nombre de usuario, con un punto de presencia
    // que no significa nada para un grupo.
    if (!knownGroupKeys_.count(key)) {
      ensureConversationListed(key, QString::fromStdString(key));
    }

    std::vector<ChatLine> lines;
    for (const auto& hl : localStore_.loadHistory(key)) {
      lines.push_back(ChatLine{static_cast<LineKind>(hl.kind), QString::fromStdString(hl.who),
                               QString::fromStdString(hl.text), hl.createdAt, hl.rawHtml});
    }
    conversations_[key] = std::move(lines);
  }

  renderActiveConversation();
}

void MainWindow::renderActiveConversation() {
  // El documento se reconstruye entero -- cualquier busqueda en curso
  // (posicion del cursor, resaltado) queda invalida.
  resetSearchState();

  chatView_->clear();
  qint64 lastShownDay = 0;
  bool haveLastShownDay = false;
  for (const ChatLine& line : conversations_[activeConversation_]) {
    if (line.timestamp > 0 && (!haveLastShownDay || !sameLocalDay(lastShownDay, line.timestamp))) {
      chatView_->append(dateDividerHtml(line.timestamp));
      lastShownDay = line.timestamp;
      haveLastShownDay = true;
    }
    chatView_->append(formatLine(line));
  }
}

// --- Busqueda dentro de la conversacion abierta ---

void MainWindow::onSearchToggleClicked() {
  if (searchBarWidget_->isVisible()) {
    closeSearchBar();
  } else {
    searchBarWidget_->setVisible(true);
    searchEdit_->setFocus();
    searchEdit_->selectAll();
  }
}

void MainWindow::closeSearchBar() {
  searchBarWidget_->setVisible(false);
  resetSearchState();
}

void MainWindow::resetSearchState() {
  searchEdit_->blockSignals(true);
  searchEdit_->clear();
  searchEdit_->blockSignals(false);
  searchCountLabel_->clear();
  // Quita el resaltado de cualquier coincidencia previa y deja el cursor al
  // principio, listo para la proxima busqueda.
  QTextCursor cursor = chatView_->textCursor();
  cursor.clearSelection();
  cursor.movePosition(QTextCursor::Start);
  chatView_->setTextCursor(cursor);
}

void MainWindow::updateSearchMatchCount() {
  QString query = searchEdit_->text();
  if (query.isEmpty()) {
    searchCountLabel_->clear();
    return;
  }

  QString plain = chatView_->toPlainText();
  int count = 0;
  int from = 0;
  while (true) {
    int idx = plain.indexOf(query, from, Qt::CaseInsensitive);
    if (idx < 0) break;
    ++count;
    from = idx + query.length();
  }
  searchCountLabel_->setText(count > 0 ? tr("%n resultado(s)", "", count) : tr("sin resultados"));
}

void MainWindow::onSearchTextChanged(const QString& text) {
  updateSearchMatchCount();
  if (text.isEmpty()) return;

  // Cada tecleo relanza la busqueda desde el principio del documento -- asi
  // siempre salta a la primera coincidencia del texto que se acaba de
  // escribir, en vez de seguir desde donde quedo la busqueda anterior.
  QTextCursor cursor = chatView_->textCursor();
  cursor.movePosition(QTextCursor::Start);
  chatView_->setTextCursor(cursor);
  chatView_->find(text);
}

void MainWindow::onSearchNextClicked() {
  QString query = searchEdit_->text();
  if (query.isEmpty()) return;

  if (chatView_->find(query)) return;

  // No hay mas coincidencias hacia adelante -- se da la vuelta al principio
  // del documento y se intenta una vez mas.
  QTextCursor cursor = chatView_->textCursor();
  cursor.movePosition(QTextCursor::Start);
  chatView_->setTextCursor(cursor);
  chatView_->find(query);
}

void MainWindow::onSearchPrevClicked() {
  QString query = searchEdit_->text();
  if (query.isEmpty()) return;

  if (chatView_->find(query, QTextDocument::FindBackward)) return;

  // Igual que onSearchNextClicked pero dando la vuelta al final.
  QTextCursor cursor = chatView_->textCursor();
  cursor.movePosition(QTextCursor::End);
  chatView_->setTextCursor(cursor);
  chatView_->find(query, QTextDocument::FindBackward);
}

QListWidgetItem* MainWindow::findConversationItem(const std::string& key) const {
  for (int i = 0; i < conversationList_->count(); ++i) {
    QListWidgetItem* item = conversationList_->item(i);
    if (item->data(Qt::UserRole).toString().toStdString() == key) return item;
  }
  return nullptr;
}

void MainWindow::selectConversation(const std::string& key) {
  if (QListWidgetItem* item = findConversationItem(key)) {
    conversationList_->setCurrentItem(item);
  }
}

void MainWindow::updateSidebarUnreadStyle(const std::string& peerKey) {
  QListWidgetItem* item = findConversationItem(peerKey);
  if (!item) return;

  int count = 0;
  auto it = unreadCounts_.find(peerKey);
  if (it != unreadCounts_.end()) count = it->second;

  QFont font = item->font();
  font.setBold(count > 0);
  item->setFont(font);

  QString label = displayLabelFor(peerKey);
  item->setText(count > 0 ? QString("%1 (%2)").arg(label).arg(count) : label);
}

QString MainWindow::displayLabelFor(const std::string& key) const {
  auto it = groups_.find(key);
  if (it != groups_.end()) return QString::fromStdString(it->second.name);
  return QString::fromStdString(key);
}

void MainWindow::markUnread(const std::string& peerKey) {
  int newCount = ++unreadCounts_[peerKey];
  updateSidebarUnreadStyle(peerKey);

  if (localStore_.isUnlocked()) {
    try {
      localStore_.setUnreadCount(peerKey, newCount);
    } catch (const std::exception& e) {
      qWarning() << "LocalStore: fallo guardando contador de no leidos de"
                 << QString::fromStdString(peerKey) << ":" << e.what();
    }
  }
}

void MainWindow::clearUnread(const std::string& peerKey) {
  auto it = unreadCounts_.find(peerKey);
  if (it == unreadCounts_.end() || it->second == 0) return;  // ya estaba limpio

  unreadCounts_[peerKey] = 0;
  updateSidebarUnreadStyle(peerKey);

  if (localStore_.isUnlocked()) {
    try {
      localStore_.setUnreadCount(peerKey, 0);
    } catch (const std::exception& e) {
      qWarning() << "LocalStore: fallo limpiando contador de no leidos de"
                 << QString::fromStdString(peerKey) << ":" << e.what();
    }
  }
}

void MainWindow::loadUnreadCountsFromStore() {
  unreadCounts_ = localStore_.loadUnreadCounts();
  for (const auto& [key, count] : unreadCounts_) {
    (void)count;
    updateSidebarUnreadStyle(key);
  }
}

void MainWindow::onConversationSelected(QListWidgetItem* current, QListWidgetItem*) {
  if (!current) return;
  activeConversation_ = current->data(Qt::UserRole).toString().toStdString();
  renderActiveConversation();

  if (activeConversation_ != kSystemKey) clearUnread(activeConversation_);

  bool isRealChat = (activeConversation_ != kSystemKey);
  messageEdit_->setEnabled(isRealChat && loggedIn_);
  sendButton_->setEnabled(isRealChat && loggedIn_);
  emojiButton_->setEnabled(isRealChat && loggedIn_);
  attachButton_->setEnabled(isRealChat && loggedIn_);
  updateGroupHeader();
}

void MainWindow::onNewChatClicked() {
  std::string peer = newChatPeerEdit_->text().toStdString();
  if (peer.empty() || peer == kSystemKey) return;

  ensureConversationListed(peer, QString::fromStdString(peer));
  selectConversation(peer);
  newChatPeerEdit_->clear();
}

void MainWindow::sendEncryptedToServer(const std::string& peer, const Bytes& ciphertext) {
  Writer w;
  w.str(peer);
  w.u8(0);
  w.blob(Bytes());
  w.blob(Bytes());
  w.blob(ciphertext);
  w.blob(Bytes());  // usedOneTimePrekeyPub: solo aplica al primer mensaje
  net_.sendFrame(MsgType::SendMsg, w.take());
}

// --- Grupos ---

void MainWindow::sendGroupCiphertextToServer(const std::string& groupId,
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

void MainWindow::sendGroupMessage(const std::string& groupId, const std::string& text) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;

  // Se registra la linea propia una sola vez, ya (optimista): igual que al
  // mandar a un peer con sesion ya establecida, no se espera a que cada
  // reparto individual confirme para mostrarla.
  logChat(groupId, LineKind::Own, QString::fromStdString(myUsername_), QString::fromStdString(text));

  std::vector<std::string> recipients;
  for (const std::string& member : it->second.members) {
    if (member != myUsername_) recipients.push_back(member);
  }

  activeGroupFanout_ = GroupFanout{groupId, MessagePayload::encodeText(text), std::move(recipients)};
  continueGroupFanout();
}

void MainWindow::continueGroupFanout() {
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
      // El protocolo PrekeyBundle no indica a quien pertenece la
      // respuesta -- solo puede haber una peticion de bootstrap en vuelo a
      // la vez, asi que el resto de la cola se para aqui y se reanuda desde
      // el case PrekeyBundle/PrekeyBundleErr de onFrameReceived.
      pendingOutbound_ = PendingOutbound{member, fanout.groupId, fanout.payload, QString()};
      Writer w;
      w.str(member);
      net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
      return;
    }
  }

  activeGroupFanout_.reset();
}

void MainWindow::updateGroupHeader() {
  auto it = groups_.find(activeConversation_);
  if (it == groups_.end()) {
    groupInfoLabel_->setVisible(false);
    addMemberButton_->setVisible(false);
    kickButton_->setVisible(false);
    leaveGroupButton_->setVisible(false);
    return;
  }

  const GroupInfo& info = it->second;
  groupInfoLabel_->setText(tr("Grupo: %1 miembro(s) -- admin: %2")
                               .arg(info.members.size())
                               .arg(QString::fromStdString(info.adminUsername)));
  groupInfoLabel_->setVisible(true);

  bool isAdmin = (info.adminUsername == myUsername_);
  addMemberButton_->setVisible(isAdmin);
  kickButton_->setVisible(isAdmin);
  kickButton_->setEnabled(isAdmin && info.members.size() > 1);
  leaveGroupButton_->setVisible(true);  // cualquier miembro puede salir, no solo el admin
}

void MainWindow::removeGroupFromSidebar(const std::string& groupId) {
  if (QListWidgetItem* item = findConversationItem(groupId)) {
    delete conversationList_->takeItem(conversationList_->row(item));
  }
  listedKeys_.erase(groupId);
  if (activeConversation_ == groupId) selectConversation(kSystemKey);
}

void MainWindow::onCreateGroupClicked() {
  if (!loggedIn_) return;

  QStringList contacts;
  for (const std::string& key : listedKeys_) {
    if (key == kSystemKey || groups_.count(key)) continue;
    contacts << QString::fromStdString(key);
  }
  contacts.sort();

  CreateGroupDialog dialog(contacts, this);
  if (dialog.exec() != QDialog::Accepted) return;

  QString name = dialog.resultName();
  if (name.isEmpty()) return;

  pendingGroupInvitees_.clear();
  for (const QString& contact : dialog.resultSelectedContacts()) {
    pendingGroupInvitees_.push_back(contact.toStdString());
  }

  Writer w;
  w.str(name.toStdString());
  net_.sendFrame(MsgType::CreateGroup, w.take());
}

void MainWindow::onKickClicked() {
  auto it = groups_.find(activeConversation_);
  if (it == groups_.end() || it->second.adminUsername != myUsername_) return;

  QStringList candidates;
  for (const std::string& member : it->second.members) {
    if (member != myUsername_) candidates << QString::fromStdString(member);
  }
  if (candidates.isEmpty()) return;

  bool ok = false;
  QString chosen = QInputDialog::getItem(this, tr("Expulsar miembro"), tr("Elige a quien expulsar:"),
                                         candidates, 0, /*editable=*/false, &ok);
  if (!ok || chosen.isEmpty()) return;

  Writer w;
  w.str(activeConversation_);
  w.str(chosen.toStdString());
  net_.sendFrame(MsgType::KickFromGroup, w.take());
}

void MainWindow::onLeaveGroupClicked() {
  auto it = groups_.find(activeConversation_);
  if (it == groups_.end()) return;
  std::string groupId = activeConversation_;
  QString name = displayLabelFor(groupId);

  Writer w;
  w.str(groupId);
  net_.sendFrame(MsgType::LeaveGroup, w.take());

  // Salir es una accion propia -- se refleja ya en la UI sin esperar
  // confirmacion del servidor (a diferencia de una expulsion, que la
  // decide otra persona y por eso si hay que esperar su notificacion).
  groups_.erase(groupId);
  removeGroupFromSidebar(groupId);
  logSystem(tr("Has salido del grupo '%1'.").arg(name));
}

void MainWindow::onAddMemberClicked() {
  auto it = groups_.find(activeConversation_);
  if (it == groups_.end() || it->second.adminUsername != myUsername_) return;

  bool ok = false;
  QString username = QInputDialog::getText(this, tr("Anadir miembro"), tr("Usuario a invitar:"),
                                           QLineEdit::Normal, "", &ok)
                         .trimmed();
  if (!ok || username.isEmpty()) return;

  Writer w;
  w.str(activeConversation_);
  w.str(username.toStdString());
  net_.sendFrame(MsgType::InviteToGroup, w.take());
}

void MainWindow::onGroupInviteReceived(uint32_t inviteId, const std::string& groupName,
                                       const std::string& inviter) {
  auto* item = new QListWidgetItem(
      tr("%1 (invitado por %2)")
          .arg(QString::fromStdString(groupName), QString::fromStdString(inviter)));
  item->setData(Qt::UserRole, static_cast<qulonglong>(inviteId));
  inviteList_->addItem(item);
  updateInvitePanelVisibility();

  logSystem(tr("%1 te invito al grupo '%2' -- puedes aceptarla o rechazarla en el panel de "
               "invitaciones de la barra lateral.")
                .arg(QString::fromStdString(inviter), QString::fromStdString(groupName)));
}

void MainWindow::respondToGroupInvite(uint32_t inviteId, bool accept) {
  Writer w;
  w.u32(inviteId);
  net_.sendFrame(accept ? MsgType::AcceptGroupInvite : MsgType::RejectGroupInvite, w.take());
}

void MainWindow::onAcceptInviteClicked() { respondToSelectedInvite(true); }

void MainWindow::onRejectInviteClicked() { respondToSelectedInvite(false); }

void MainWindow::respondToSelectedInvite(bool accept) {
  QListWidgetItem* item = inviteList_->currentItem();
  if (!item) item = inviteList_->item(0);
  if (!item) return;

  uint32_t inviteId = static_cast<uint32_t>(item->data(Qt::UserRole).toULongLong());
  respondToGroupInvite(inviteId, accept);

  delete inviteList_->takeItem(inviteList_->row(item));
  updateInvitePanelVisibility();
}

void MainWindow::updateInvitePanelVisibility() {
  bool hasInvites = inviteList_->count() > 0;
  inviteLabel_->setVisible(hasInvites);
  inviteList_->setVisible(hasInvites);
  acceptInviteButton_->setVisible(hasInvites);
  rejectInviteButton_->setVisible(hasInvites);
}

// --- Transferencia de archivos ---

QString MainWindow::uniqueDownloadPath(const QString& dir, const QString& filename) const {
  QString candidate = dir + "/" + filename;
  if (!QFile::exists(candidate)) return candidate;

  QFileInfo info(filename);
  QString base = info.completeBaseName();
  QString ext = info.suffix();
  for (int i = 1;; ++i) {
    QString altName =
        ext.isEmpty() ? QString("%1 (%2)").arg(base).arg(i) : QString("%1 (%2).%3").arg(base).arg(i).arg(ext);
    QString altPath = dir + "/" + altName;
    if (!QFile::exists(altPath)) return altPath;
  }
}

void MainWindow::onEmojiButtonClicked() {
  auto* picker = new EmojiPicker(this);
  connect(picker, &EmojiPicker::emojiSelected, this, &MainWindow::insertEmoji);

  // Se posiciona justo encima del boton que lo abrio, no en el centro de la
  // ventana -- se comporta como un desplegable, no como un dialogo aparte.
  QPoint pos = emojiButton_->mapToGlobal(QPoint(0, -picker->height()));
  picker->move(pos);
  picker->show();
}

void MainWindow::insertEmoji(const QString& emoji) {
  messageEdit_->insert(emoji);
  messageEdit_->setFocus();
}

void MainWindow::onAttachClicked() {
  if (!loggedIn_) return;

  QString path = QFileDialog::getOpenFileName(this, tr("Selecciona un archivo para enviar"));
  if (path.isEmpty()) return;

  startOutgoingFileTransfer(path);
}

void MainWindow::startOutgoingFileTransfer(const QString& path) {
  std::string peer = activeConversation_;
  if (peer.empty() || peer == kSystemKey) {
    logSystem(tr("Selecciona una conversacion antes de adjuntar un archivo."));
    return;
  }
  if (peer == myUsername_) {
    startSelfFileAttach(path);
    return;
  }
  bool isGroup = groups_.count(peer) != 0;
  if (!isGroup && !crypto_.hasSession(peer)) {
    logSystem(tr("Manda primero un mensaje de texto a %1 para establecer la conversacion antes "
                 "de enviar archivos.")
                  .arg(QString::fromStdString(peer)));
    return;
  }
  // Ya no hace falta que el destinatario este en linea: el archivo se sube
  // al servidor de una vez, y solo un puntero pequeno (FileBlobPointer) va
  // por su canal cifrado -- ese si se pone en cola normal si esta
  // desconectado, igual que un mensaje de texto.
  if (outgoingTransfer_ || activeDownload_) {
    logSystem(tr("Ya hay una transferencia de archivo en curso, espera a que termine."));
    return;
  }

  QFileInfo info(path);
  qint64 size = info.size();
  if (size <= 0) {
    logSystem(tr("No se pudo leer el archivo seleccionado."));
    return;
  }
  if (static_cast<uint64_t>(size) > kMaxFileSize) {
    logSystem(tr("El archivo supera el limite de %1 MB de esta version.")
                 .arg(kMaxFileSize / (1024 * 1024)));
    return;
  }

  auto file = std::make_unique<QFile>(path);
  if (!file->open(QIODevice::ReadOnly)) {
    logSystem(tr("No se pudo abrir el archivo seleccionado para leerlo."));
    return;
  }

  OutgoingTransfer transfer;
  transfer.peer = peer;
  transfer.isGroup = isGroup;
  transfer.filename = info.fileName();
  transfer.totalSize = static_cast<uint64_t>(size);
  transfer.file = std::move(file);
  transfer.key = templar::crypto::generateFileKey();
  transfer.encryptor = std::make_unique<templar::crypto::FileEncryptor>(transfer.key);
  outgoingTransfer_ = std::move(transfer);

  Writer w;
  w.u64(outgoingTransfer_->totalSize);
  w.str(outgoingTransfer_->filename.toStdString());
  net_.sendFrame(MsgType::UploadBlobBegin, w.take());

  transferLabel_->setText(tr("Subiendo: %1").arg(outgoingTransfer_->filename));
  transferProgress_->setValue(0);
  transferLabel_->setVisible(true);
  transferProgress_->setVisible(true);
  // fileSendTimer_ arranca al llegar UploadBlobBeginOk con el blobId (ver
  // onFrameReceived) -- todavia no hay nada que mandar sin el.
}

void MainWindow::startSelfFileAttach(const QString& path) {
  QFileInfo info(path);
  if (!info.exists() || !info.isFile()) {
    logSystem(tr("No se pudo leer el archivo seleccionado."));
    return;
  }

  // Se copia (no se referencia la ruta original) para que el enlace siga
  // funcionando aunque el archivo original se mueva, se renombre o se
  // borre despues -- coherente con lo que se espera de una nota guardada.
  QString destDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/mis_archivos";
  QDir().mkpath(destDir);
  QString destPath = uniqueDownloadPath(destDir, info.fileName());

  if (!QFile::copy(path, destPath)) {
    logSystem(tr("No se pudo guardar una copia local del archivo."));
    return;
  }

  QFileInfo destInfo(destPath);
  double mb = static_cast<double>(destInfo.size()) / (1024.0 * 1024.0);
  QString sizeText = QString::number(mb, 'f', mb < 0.1 ? 3 : 1) + " MB";
  // rawHtml=true igual que el enlace de descarga normal -- este SI se
  // puede persistir tal cual (a diferencia de un puntero de blob, esto no
  // depende de ninguna clave que solo viva en memoria).
  QString link = "<a href='templar-selffile:" +
                QString::fromLatin1(QUrl::toPercentEncoding(destPath)) + "'>📄 " +
                info.fileName().toHtmlEscaped() + " (" + sizeText + ")</a>";
  logChat(myUsername_, LineKind::Own, QString::fromStdString(myUsername_), link, /*rawHtml=*/true);
}

void MainWindow::sendNextFileChunk() {
  if (!outgoingTransfer_ || outgoingTransfer_->blobId.empty()) {
    fileSendTimer_->stop();
    return;
  }
  auto& t = *outgoingTransfer_;

  QByteArray buf = t.file->read(kFileChunkSize);
  // A diferencia del hash-al-final de antes: crypto_secretstream necesita
  // saber EN EL MOMENTO si un fragmento es el ultimo (para marcarlo TAG_FINAL),
  // no se puede anadir esa marca despues -- se calcula sin necesidad de
  // "mirar hacia adelante" porque ya sabemos t.totalSize de antemano.
  bool isLast = (t.sentBytes + static_cast<uint64_t>(buf.size())) >= t.totalSize;

  Bytes plainChunk(buf.constData(), buf.constData() + buf.size());
  Bytes cipherChunk = t.encryptor->encryptChunk(plainChunk, isLast);

  Writer w;
  w.str(t.blobId);
  w.blob(cipherChunk);
  net_.sendFrame(MsgType::UploadBlobChunk, w.take());

  t.sentBytes += static_cast<uint64_t>(buf.size());
  int percent = t.totalSize > 0 ? static_cast<int>((t.sentBytes * 100) / t.totalSize) : 100;
  transferProgress_->setValue(percent);

  if (isLast) {
    fileSendTimer_->stop();
    Writer end;
    end.str(t.blobId);
    net_.sendFrame(MsgType::UploadBlobEnd, end.take());
  }
}

void MainWindow::sendBlobPointerAndFinish() {
  if (!outgoingTransfer_) return;
  OutgoingTransfer t = std::move(*outgoingTransfer_);
  outgoingTransfer_.reset();
  t.file->close();

  Bytes keyBytes(t.key.begin(), t.key.end());
  Bytes headerBytes(t.encryptor->header().begin(), t.encryptor->header().end());
  Bytes pointerPayload = MessagePayload::encodeFileBlobPointer(t.blobId, t.filename.toStdString(),
                                                                t.totalSize, keyBytes, headerBytes);

  transferProgress_->setVisible(false);
  transferLabel_->setVisible(false);

  if (t.isGroup) {
    // Mismo mecanismo de fan-out que un mensaje de texto de grupo -- la
    // subida ya se hizo UNA vez, esto solo manda un puntero pequeno por
    // miembro.
    logChat(t.peer, LineKind::Own, QString::fromStdString(myUsername_),
           tr("Archivo enviado: %1").arg(t.filename));

    std::vector<std::string> recipients;
    auto it = groups_.find(t.peer);
    if (it != groups_.end()) {
      for (const std::string& member : it->second.members) {
        if (member != myUsername_) recipients.push_back(member);
      }
    }
    activeGroupFanout_ = GroupFanout{t.peer, pointerPayload, std::move(recipients)};
    continueGroupFanout();
  } else if (crypto_.hasSession(t.peer)) {
    Bytes ciphertext = crypto_.encryptNext(t.peer, pointerPayload);
    sendEncryptedToServer(t.peer, ciphertext);
    logChat(t.peer, LineKind::Own, QString::fromStdString(myUsername_),
           tr("Archivo enviado: %1").arg(t.filename));
    trySaveSession(t.peer);
  } else {
    // Sesion perdida entre que se empezo la subida y que termino (raro,
    // pero posible si p.ej. se borro el almacen local a mitad) -- se
    // bootstrea igual que un mensaje de texto normal sin sesion.
    pendingOutbound_ =
        PendingOutbound{t.peer, "", pointerPayload, tr("Archivo enviado: %1").arg(t.filename)};
    Writer w;
    w.str(t.peer);
    net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
  }
}

void MainWindow::onFileBlobPointerReceived(const std::string& originKey, const std::string& sender,
                                           const std::string& blobId, const QString& filename,
                                           uint64_t fileSize, const Bytes& fileKey,
                                           const Bytes& fileHeader) {
  if (fileSize > kMaxFileSize || fileKey.size() != templar::crypto::kFileKeyBytes ||
      fileHeader.size() != templar::crypto::kFileHeaderBytes) {
    logSystem(tr("Se rechaza un puntero de archivo invalido de %1.")
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
    } catch (const std::exception& e) {
      qWarning() << "LocalStore: fallo guardando un puntero de archivo pendiente:" << e.what();
    }
  }

  double mb = static_cast<double>(fileSize) / (1024.0 * 1024.0);
  QString sizeText = QString::number(mb, 'f', mb < 0.1 ? 3 : 1) + " MB";
  // rawHtml=true: la unica linea que nosotros mismos generamos con HTML de
  // proposito (ver el comentario de ChatLine::rawHtml) -- el nombre del
  // archivo SI se escapa a mano porque ese si viene de otra persona. Se
  // persiste este mismo enlace (no un texto de repuesto): como la clave
  // ahora tambien se guarda (ver savePendingBlobDownload arriba), el
  // enlace SIGUE funcionando tras reiniciar la app, hasta que se descargue
  // o el blob caduque en el servidor (30 dias).
  QString link = "<a href='templar-download:" + QString::fromStdString(blobId) + "'>⬇ " +
                tr("Descargar") + " " + filename.toHtmlEscaped() + " (" + sizeText + ")</a>";
  logChat(originKey, LineKind::Peer, QString::fromStdString(sender), link, /*rawHtml=*/true);
  if (originKey != activeConversation_) markUnread(originKey);
}

void MainWindow::startBlobDownload(const std::string& blobId) {
  auto it = pendingBlobDownloads_.find(blobId);
  if (it == pendingBlobDownloads_.end()) {
    logSystem(tr("Ese enlace de descarga ya no esta disponible en esta sesion."));
    return;
  }
  if (activeDownload_) {
    logSystem(tr("Ya hay una descarga en curso, espera a que termine."));
    return;
  }

  PendingBlobDownload pending = std::move(it->second);
  pendingBlobDownloads_.erase(it);
  // Se borra de LocalStore aqui, al empezar la descarga -- no al terminar
  // con exito -- para que coincida exactamente con el borrado del mapa en
  // memoria de arriba: si la descarga falla o el archivo llega corrupto, no
  // queda un puntero persistido "zombie" sin entrada en memoria que lo
  // respalde (el usuario tendria que pedir que se lo reenvien de todos
  // modos, ver onBlobDataReceived/onBlobNotFound).
  if (localStore_.isUnlocked()) {
    try {
      localStore_.deletePendingBlobDownload(blobId);
    } catch (const std::exception& e) {
      qWarning() << "LocalStore: fallo borrando un puntero de archivo pendiente:" << e.what();
    }
  }

  QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  QDir().mkpath(downloadsDir);
  // .fileName() descarta cualquier componente de ruta (p.ej.
  // "../../etc/passwd") -- nunca hay que confiar en el nombre que manda el
  // otro lado como una ruta real.
  QString safeName = QFileInfo(pending.filename).fileName();
  if (safeName.isEmpty()) safeName = tr("archivo_recibido");
  QString destPath = uniqueDownloadPath(downloadsDir, safeName);

  auto file = std::make_unique<QFile>(destPath);
  if (!file->open(QIODevice::WriteOnly)) {
    logSystem(tr("No se pudo crear el archivo de destino para la descarga."));
    return;
  }

  ActiveBlobDownload active;
  active.blobId = blobId;
  active.originKey = pending.originKey;
  active.sender = pending.sender;
  active.filename = QFileInfo(destPath).fileName();
  active.totalSize = pending.fileSize;
  active.file = std::move(file);
  active.decryptor = std::make_unique<templar::crypto::FileDecryptor>(pending.key, pending.header);
  activeDownload_ = std::move(active);

  transferLabel_->setText(tr("Descargando: %1").arg(activeDownload_->filename));
  transferProgress_->setValue(0);
  transferLabel_->setVisible(true);
  transferProgress_->setVisible(true);

  Writer w;
  w.str(blobId);
  net_.sendFrame(MsgType::DownloadBlob, w.take());
}

void MainWindow::onBlobDataReceived(const std::string& blobId, const Bytes& chunk) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  auto& d = *activeDownload_;

  try {
    templar::crypto::FileDecryptor::Result result = d.decryptor->decryptChunk(chunk);
    d.file->write(reinterpret_cast<const char*>(result.plaintext.data()),
                 static_cast<qint64>(result.plaintext.size()));
    d.receivedBytes += result.plaintext.size();
    if (result.isLast) d.finalTagSeen = true;

    int percent =
        d.totalSize > 0 ? static_cast<int>((d.receivedBytes * 100) / d.totalSize) : 100;
    transferProgress_->setValue(percent);
  } catch (const std::exception& e) {
    logChat(d.originKey, LineKind::Peer, QString::fromStdString(d.sender),
           tr("[ALERTA] Fallo descifrando '%1': %2").arg(d.filename, QString::fromUtf8(e.what())));
    activeDownload_.reset();
    transferProgress_->setVisible(false);
    transferLabel_->setVisible(false);
  }
}

void MainWindow::onBlobEndReceived(const std::string& blobId) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  ActiveBlobDownload d = std::move(*activeDownload_);
  activeDownload_.reset();

  QString savedPath = d.file->fileName();
  d.file->close();

  transferProgress_->setVisible(false);
  transferLabel_->setVisible(false);

  if (d.finalTagSeen) {
    logChat(d.originKey, LineKind::Peer, QString::fromStdString(d.sender),
           tr("Archivo '%1' recibido y verificado -> %2").arg(d.filename, savedPath));
  } else {
    logChat(d.originKey, LineKind::Peer, QString::fromStdString(d.sender),
           tr("[ALERTA] El archivo '%1' llego incompleto (se corto en transito) -- no te fies "
              "del contenido.")
               .arg(d.filename));
  }
}

void MainWindow::onBlobNotFound(const std::string& blobId, const std::string& reason) {
  if (!activeDownload_ || activeDownload_->blobId != blobId) return;
  ActiveBlobDownload d = std::move(*activeDownload_);
  activeDownload_.reset();
  d.file->remove();

  transferProgress_->setVisible(false);
  transferLabel_->setVisible(false);

  logChat(d.originKey, LineKind::Peer, QString::fromStdString(d.sender),
         tr("[ALERTA] No se pudo descargar '%1': %2")
             .arg(d.filename, QString::fromStdString(reason)));
}

void MainWindow::cancelActiveTransfers() {
  fileSendTimer_->stop();
  outgoingTransfer_.reset();
  activeDownload_.reset();
  transferProgress_->setVisible(false);
  transferLabel_->setVisible(false);
}

// --- UI state ---

void MainWindow::setConnectedUiState(bool connected) {
  connectButton_->setEnabled(!connected);
  hostEdit_->setEnabled(!connected);
  portEdit_->setEnabled(!connected);
  usernameEdit_->setEnabled(connected);
  passwordEdit_->setEnabled(connected);
  registerButton_->setEnabled(connected);
  loginButton_->setEnabled(connected);
  if (!connected) setLoggedInUiState(false);
}

void MainWindow::setLoginError(const QString& message) {
  loginErrorLabel_->setText(message);
  loginErrorLabel_->setVisible(!message.isEmpty());
}

void MainWindow::setLoggedInUiState(bool loggedIn) {
  newChatPeerEdit_->setEnabled(loggedIn);
  newChatButton_->setEnabled(loggedIn);
  createGroupButton_->setEnabled(loggedIn);
  bool isRealChat = (activeConversation_ != kSystemKey);
  messageEdit_->setEnabled(loggedIn && isRealChat);
  sendButton_->setEnabled(loggedIn && isRealChat);
  emojiButton_->setEnabled(loggedIn && isRealChat);
  attachButton_->setEnabled(loggedIn && isRealChat);
}

// --- Slots de conexion/autenticacion ---

void MainWindow::onConnectClicked() {
  setLoginError("");
  statusLabel_->setText(tr("Conectando..."));
  net_.connectToServer(hostEdit_->text(), static_cast<quint16>(portEdit_->text().toUInt()));
}

void MainWindow::onDisconnectClicked() { net_.disconnectFromServer(); }

Bytes MainWindow::buildRegisterPayload(const std::string& username,
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

void MainWindow::onRegisterClicked() {
  setLoginError("");
  std::string username = usernameEdit_->text().toStdString();
  std::string password = passwordEdit_->text().toStdString();
  if (username.empty() || password.size() < 8) {
    setLoginError(
        tr("El usuario no puede estar vacio y la contrasena necesita al menos 8 caracteres."));
    return;
  }
  net_.sendFrame(MsgType::Register, buildRegisterPayload(username, password));
}

void MainWindow::onLoginClicked() {
  setLoginError("");
  std::string username = usernameEdit_->text().toStdString();
  std::string password = passwordEdit_->text().toStdString();

  // La necesitaremos en el handler de LoginOk para desbloquear el almacen
  // local (que reusa esta misma contrasena) -- capturada aqui en vez de
  // releer passwordEdit_ mas tarde, por si el usuario edita el campo
  // mientras el login esta en vuelo.
  pendingLoginPassword_ = password;

  Writer w;
  w.str(username);
  w.str(password);
  net_.sendFrame(MsgType::Login, w.take());
}

// --- Slots de chat ---

void MainWindow::onSendClicked() {
  if (!loggedIn_) return;

  std::string peer = activeConversation_;
  std::string text = messageEdit_->text().toStdString();
  if (peer.empty() || peer == kSystemKey || text.empty()) return;

  if (peer == myUsername_) {
    // Chat contigo mismo: una nota local, no un mensaje de verdad -- no
    // hay a quien mas llegarle, asi que no tiene sentido cifrar/mandar
    // nada por la red (ver el comentario de startSelfFileAttach).
    logChat(peer, LineKind::Own, QString::fromStdString(myUsername_), QString::fromStdString(text));
    messageEdit_->clear();
    return;
  }

  if (groups_.count(peer)) {
    sendGroupMessage(peer, text);
  } else if (crypto_.hasSession(peer)) {
    Bytes ciphertext = crypto_.encryptNext(peer, MessagePayload::encodeText(text));
    sendEncryptedToServer(peer, ciphertext);
    logChat(peer, LineKind::Own, QString::fromStdString(myUsername_), QString::fromStdString(text));
    trySaveSession(peer);
  } else {
    // Necesitamos el prekey bundle de B antes de poder cifrar el primer
    // mensaje (X3DH). Se completa en onFrameReceived cuando llegue.
    pendingOutbound_ =
        PendingOutbound{peer, "", MessagePayload::encodeText(text), QString::fromStdString(text)};
    Writer w;
    w.str(peer);
    net_.sendFrame(MsgType::FetchPrekeyBundle, w.take());
  }
  messageEdit_->clear();
}

void MainWindow::onNetConnected() {
  setConnectedUiState(true);
  QString endpoint = hostEdit_->text() + ":" + portEdit_->text();
  statusLabel_->setText(tr("Conectado a %1").arg(endpoint));
  logSystem(tr("Conectado a %1").arg(endpoint));
}

void MainWindow::onNetDisconnected() {
  loggedIn_ = false;
  setConnectedUiState(false);
  passwordEdit_->clear();
  statusLabel_->setText(tr("Desconectado."));
  logSystem(tr("Conexion cerrada."));
  cancelActiveTransfers();
  pendingBlobDownloads_.clear();
  localStore_.lock();
  stack_->setCurrentWidget(loginPage_);
}

void MainWindow::onNetError(QString message) {
  statusLabel_->setText(tr("Error: %1").arg(message));
  logSystem(tr("Error de red: %1").arg(message));
}

void MainWindow::onFrameReceived(MsgType type, Bytes payload) {
  try {
    switch (type) {
      case MsgType::RegisterOk: {
        setLoginError("");
        logSystem(tr("Registro exitoso. Ahora puedes iniciar sesion."));

        // Una cuenta recien registrada NUNCA debe heredar el almacen local
        // de una cuenta anterior con el mismo nombre de usuario (p.ej. de
        // pruebas contra otro servidor) -- eso dejaba la identidad del
        // cliente sin coincidir con la que el servidor acaba de guardar, y
        // nadie podia completar el intercambio de claves. Se limpia y se
        // guarda ya la identidad recien registrada.
        try {
          std::string username = usernameEdit_->text().toStdString();
          std::string password = passwordEdit_->text().toStdString();
          localStore_.resetAndUnlock(username, password);

          PersistedIdentity toSave;
          toSave.ed = crypto_.identity().signingKeys();
          toSave.x = crypto_.identity().exchangeKeys();
          toSave.signedPrekey = crypto_.signedPrekey();
          toSave.signedPrekeySig = crypto_.signedPrekeySignature();
          localStore_.saveIdentity(toSave);
          localStore_.lock();  // todavia no hemos iniciado sesion de verdad
        } catch (const std::exception& e) {
          qWarning() << "LocalStore: fallo preparando el almacen tras registrar:" << e.what();
        }
        break;
      }
      case MsgType::RegisterErr: {
        Reader r(payload);
        QString reason = QString::fromStdString(r.str());
        setLoginError(reason);
        logSystem(tr("Error de registro: %1").arg(reason));
        break;
      }
      case MsgType::LoginOk: {
        setLoginError("");
        loggedIn_ = true;
        myUsername_ = usernameEdit_->text().toStdString();
        setLoggedInUiState(true);
        connectedAsLabel_->setText(tr("Conectado como: %1").arg(QString::fromStdString(myUsername_)));
        logSystem(tr("Sesion iniciada como %1.").arg(usernameEdit_->text()));
        stack_->setCurrentWidget(chatPage_);

        // Se registra ANTES de loadHistoryFromStore() (mas abajo) para que
        // siempre quede como segunda entrada fija, justo despues de
        // "Sistema" -- ensureConversationListed no reordena nada despues
        // de la primera vez que se anade una clave.
        ensureConversationListed(myUsername_, tr("Tú"));

        UnlockResult unlockResult = localStore_.unlock(myUsername_, pendingLoginPassword_);
        pendingLoginPassword_.assign(pendingLoginPassword_.size(), '\0');
        pendingLoginPassword_.clear();

        if (unlockResult == UnlockResult::WrongPassword) {
          logSystem(
              tr("No se pudo desbloquear el almacen local (¿cambiaste la contrasena de cuenta "
                 "desde otro dispositivo?). Esta sesion no se guardara."));
        } else if (unlockResult == UnlockResult::LoadedExisting) {
          // El login ya tuvo exito y la pantalla ya cambio a la de chat --
          // un problema al restaurar el almacen local (por ejemplo un
          // almacen de una version anterior con datos que ya no se pueden
          // leer) no debe echar abajo la sesion, solo degradar a "sin
          // historial/sesiones restauradas por esta vez".
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
            knownGroupKeys_.clear();
            for (const std::string& groupId : localStore_.loadKnownGroupKeys()) {
              knownGroupKeys_.insert(groupId);
            }
            loadHistoryFromStore();
            loadUnreadCountsFromStore();
            logSystem(tr("Almacen local desbloqueado: identidad, sesiones e historial restaurados."));
          } catch (const std::exception& e) {
            logSystem(tr("No se pudo restaurar el almacen local por completo: %1")
                          .arg(QString::fromUtf8(e.what())));
          }
        } else {  // CreatedNew
          PersistedIdentity toSave;
          toSave.ed = crypto_.identity().signingKeys();
          toSave.x = crypto_.identity().exchangeKeys();
          toSave.signedPrekey = crypto_.signedPrekey();
          toSave.signedPrekeySig = crypto_.signedPrekeySignature();
          localStore_.saveIdentity(toSave);
        }
        ensureOneTimePrekeys();
        break;
      }
      case MsgType::LoginErr: {
        Reader r(payload);
        QString reason = QString::fromStdString(r.str());
        setLoginError(reason);
        logSystem(tr("Error de inicio de sesion: %1").arg(reason));
        break;
      }
      case MsgType::PrekeyBundle: {
        if (!pendingOutbound_) break;
        PendingOutbound pending = *pendingOutbound_;
        pendingOutbound_.reset();

        Reader r(payload);
        PrekeyBundle bundle{};
        Bytes idX = r.blob();
        Bytes idEd = r.blob();
        Bytes spk = r.blob();
        bundle.signedPrekeySig = r.blob();
        Bytes otpk = r.blob();

        bool sizesOk = idX.size() == crypto_box_PUBLICKEYBYTES &&
                      idEd.size() == crypto_sign_PUBLICKEYBYTES &&
                      spk.size() == crypto_box_PUBLICKEYBYTES &&
                      (otpk.empty() || otpk.size() == crypto_box_PUBLICKEYBYTES);
        if (!sizesOk) {
          logSystem(tr("Bundle de prekeys con tamano invalido, se descarta."));
        } else {
          std::copy(idX.begin(), idX.end(), bundle.identityPkX25519);
          std::copy(idEd.begin(), idEd.end(), bundle.identityPkEd25519);
          std::copy(spk.begin(), spk.end(), bundle.signedPrekeyPub);
          bundle.hasOneTimePrekey = !otpk.empty();
          if (bundle.hasOneTimePrekey) {
            std::copy(otpk.begin(), otpk.end(), bundle.oneTimePrekeyPub);
          }

          if (!bundle.verify()) {
            logSystem(tr("El bundle de '%1' tiene una firma invalida -- posible intermediario. "
                         "Mensaje NO enviado.")
                          .arg(QString::fromStdString(pending.peer)));
          } else {
            OutboundFirstMessage first = crypto_.encryptFirst(pending.peer, bundle, pending.payload);

            if (pending.groupId.empty()) {
              Writer w;
              w.str(pending.peer);
              w.u8(1);
              w.blob(first.senderIdentityPkX25519);
              w.blob(first.senderEphemeralPk);
              w.blob(first.ciphertext);
              w.blob(otpk);
              net_.sendFrame(MsgType::SendMsg, w.take());

              if (!pending.ownLineText.isEmpty()) {
                logChat(pending.peer, LineKind::Own, QString::fromStdString(myUsername_),
                       pending.ownLineText);
              }
            } else {
              Writer w;
              w.str(pending.groupId);
              w.str(pending.peer);
              w.u8(1);
              w.blob(first.senderIdentityPkX25519);
              w.blob(first.senderEphemeralPk);
              w.blob(first.ciphertext);
              w.blob(otpk);
              net_.sendFrame(MsgType::SendGroupMsg, w.take());
              // El mensaje "propio" ya se registro una vez en
              // sendGroupMessage() al arrancar el fan-out.
            }
            trySaveSession(pending.peer);
          }
        }

        if (!pending.groupId.empty()) continueGroupFanout();
        break;
      }
      case MsgType::PrekeyBundleErr: {
        Reader r(payload);
        logSystem(tr("No se pudo iniciar conversacion: %1").arg(QString::fromStdString(r.str())));
        bool wasGroupFanout = pendingOutbound_ && !pendingOutbound_->groupId.empty();
        pendingOutbound_.reset();
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
        logSystem(
            tr("No se pudo empezar a subir el archivo: %1").arg(QString::fromStdString(r.str())));
        outgoingTransfer_.reset();
        transferLabel_->setVisible(false);
        transferProgress_->setVisible(false);
        break;
      }
      case MsgType::UploadBlobEndOk:
        sendBlobPointerAndFinish();
        break;
      case MsgType::UploadBlobEndErr: {
        Reader r(payload);
        logSystem(
            tr("Fallo terminando de subir el archivo: %1").arg(QString::fromStdString(r.str())));
        outgoingTransfer_.reset();
        transferLabel_->setVisible(false);
        transferProgress_->setVisible(false);
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
        onBlobNotFound(blobId, reason);
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
          case PayloadKind::Text: {
            trySaveSession(sender);
            logChat(sender, LineKind::Peer, QString::fromStdString(sender),
                   QString::fromStdString(decoded.text));
            if (sender != activeConversation_) markUnread(sender);
            if (trayIcon_ && shouldNotify()) {
              // Contenido generico a proposito: el texto real no aparece en
              // la notificacion del sistema operativo (puede verse en la
              // pantalla de bloqueo o quedar en el historial).
              trayIcon_->showMessage(QString::fromStdString(sender), tr("Nuevo mensaje"),
                                     QSystemTrayIcon::Information, 4000);
            }
            break;
          }
          case PayloadKind::FileBlobPointer:
            trySaveSession(sender);
            onFileBlobPointerReceived(sender, sender, decoded.blobId,
                                      QString::fromStdString(decoded.filename), decoded.fileSize,
                                      decoded.fileKey, decoded.fileHeader);
            if (trayIcon_ && shouldNotify()) {
              trayIcon_->showMessage(QString::fromStdString(sender), tr("Nuevo archivo"),
                                     QSystemTrayIcon::Information, 4000);
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

        Writer ack;
        ack.u32(mailboxId);
        net_.sendFrame(MsgType::Ack, ack.take());
        break;
      }
      case MsgType::PresenceUpdate: {
        Reader r(payload);
        std::string username = r.str();
        bool online = r.u8() != 0;
        if (QListWidgetItem* item = findConversationItem(username)) {
          item->setIcon(presenceIcon(online));
        }
        break;
      }
      case MsgType::GroupCreated: {
        Reader r(payload);
        std::string groupId = r.str();
        std::string name = r.str();

        GroupInfo info;
        info.id = groupId;
        info.name = name;
        info.adminUsername = myUsername_;
        info.members = {myUsername_};
        groups_[groupId] = info;
        knownGroupKeys_.insert(groupId);
        if (localStore_.isUnlocked()) {
          try {
            localStore_.rememberGroupKey(groupId);
          } catch (const std::exception& e) {
            qWarning() << "LocalStore: fallo recordando el grupo" << QString::fromStdString(groupId)
                      << ":" << e.what();
          }
        }

        ensureConversationListed(groupId, QString::fromStdString(name));
        if (QListWidgetItem* item = findConversationItem(groupId)) item->setIcon(QIcon());
        selectConversation(groupId);
        logSystem(tr("Grupo '%1' creado.").arg(QString::fromStdString(name)));

        for (const std::string& invitee : pendingGroupInvitees_) {
          Writer w;
          w.str(groupId);
          w.str(invitee);
          net_.sendFrame(MsgType::InviteToGroup, w.take());
        }
        pendingGroupInvitees_.clear();
        break;
      }
      case MsgType::GroupInvite: {
        Reader r(payload);
        uint32_t inviteId = r.u32();
        r.str();  // groupId: no hace falta guardarlo, el servidor ya lo resuelve por inviteId
        std::string groupName = r.str();
        std::string inviter = r.str();
        onGroupInviteReceived(inviteId, groupName, inviter);
        break;
      }
      case MsgType::GroupMemberJoined: {
        Reader r(payload);
        std::string groupId = r.str();
        std::string username = r.str();

        auto it = groups_.find(groupId);
        if (it != groups_.end()) {
          auto& members = it->second.members;
          if (std::find(members.begin(), members.end(), username) == members.end()) {
            members.push_back(username);
          }
          if (username != myUsername_) {
            logChat(groupId, LineKind::System, QString(),
                   tr("%1 se unio al grupo.").arg(QString::fromStdString(username)));
          }
          if (groupId == activeConversation_) updateGroupHeader();
        }
        break;
      }
      case MsgType::GroupMemberKicked: {
        Reader r(payload);
        std::string groupId = r.str();
        std::string username = r.str();

        if (username == myUsername_) {
          groups_.erase(groupId);
          removeGroupFromSidebar(groupId);
          logSystem(tr("Has sido expulsado de un grupo."));
        } else {
          auto it = groups_.find(groupId);
          if (it != groups_.end()) {
            auto& members = it->second.members;
            members.erase(std::remove(members.begin(), members.end(), username), members.end());
            logChat(groupId, LineKind::System, QString(),
                   tr("%1 fue expulsado del grupo.").arg(QString::fromStdString(username)));
            if (groupId == activeConversation_) updateGroupHeader();
          }
        }
        break;
      }
      case MsgType::GroupMemberLeft: {
        Reader r(payload);
        std::string groupId = r.str();
        std::string username = r.str();
        std::string currentAdmin = r.str();

        // Quien se va nunca recibe esta notificacion de vuelta (ya
        // actualizo su propia UI al mandar LeaveGroup), asi que este caso
        // siempre es sobre otro miembro.
        auto it = groups_.find(groupId);
        if (it != groups_.end()) {
          auto& members = it->second.members;
          members.erase(std::remove(members.begin(), members.end(), username), members.end());
          it->second.adminUsername = currentAdmin;
          logChat(groupId, LineKind::System, QString(),
                 tr("%1 salio del grupo.").arg(QString::fromStdString(username)));
          if (groupId == activeConversation_) updateGroupHeader();
        }
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
        if (decoded.kind == PayloadKind::Text) {
          logChat(groupId, LineKind::Peer, QString::fromStdString(sender),
                 QString::fromStdString(decoded.text));
          if (groupId != activeConversation_) markUnread(groupId);
          if (trayIcon_ && shouldNotify()) {
            trayIcon_->showMessage(displayLabelFor(groupId),
                                   tr("Nuevo mensaje de %1").arg(QString::fromStdString(sender)),
                                   QSystemTrayIcon::Information, 4000);
          }
        } else if (decoded.kind == PayloadKind::FileBlobPointer) {
          onFileBlobPointerReceived(groupId, sender, decoded.blobId,
                                    QString::fromStdString(decoded.filename), decoded.fileSize,
                                    decoded.fileKey, decoded.fileHeader);
          if (trayIcon_ && shouldNotify()) {
            trayIcon_->showMessage(displayLabelFor(groupId),
                                   tr("Nuevo archivo de %1").arg(QString::fromStdString(sender)),
                                   QSystemTrayIcon::Information, 4000);
          }
        }
        // FileMeta/FileChunk/FileEnd (formato viejo): se ignoran, ver el
        // comentario equivalente en el case DeliverMsg.

        Writer ack;
        ack.u32(mailboxId);
        net_.sendFrame(MsgType::Ack, ack.take());
        break;
      }
      case MsgType::MyGroups: {
        Reader r(payload);
        uint32_t count = r.u32();

        std::unordered_set<std::string> freshIds;
        for (uint32_t i = 0; i < count; ++i) {
          GroupInfo info;
          info.id = r.str();
          info.name = r.str();
          info.adminUsername = r.str();
          uint32_t memberCount = r.u32();
          info.members.reserve(memberCount);
          for (uint32_t j = 0; j < memberCount; ++j) info.members.push_back(r.str());

          freshIds.insert(info.id);
          groups_[info.id] = info;
          knownGroupKeys_.insert(info.id);
          if (localStore_.isUnlocked()) {
            try {
              localStore_.rememberGroupKey(info.id);
            } catch (const std::exception& e) {
              qWarning() << "LocalStore: fallo recordando el grupo"
                        << QString::fromStdString(info.id) << ":" << e.what();
            }
          }
          ensureConversationListed(info.id, QString::fromStdString(info.name));
          if (QListWidgetItem* item = findConversationItem(info.id)) item->setIcon(QIcon());
          updateSidebarUnreadStyle(info.id);
        }

        // Grupos que conociamos pero ya no aparecen en el snapshot (p.ej.
        // nos expulsaron mientras estabamos desconectados): se quitan.
        for (auto it = groups_.begin(); it != groups_.end();) {
          if (freshIds.count(it->first)) {
            ++it;
            continue;
          }
          removeGroupFromSidebar(it->first);
          it = groups_.erase(it);
        }
        updateGroupHeader();
        break;
      }
      case MsgType::GroupErr: {
        Reader r(payload);
        std::string context = r.str();
        QString message = QString::fromStdString(r.str());
        logSystem(
            tr("Error de grupo (%1): %2").arg(QString::fromStdString(context), message));
        break;
      }
      default:
        break;
    }
  } catch (const std::exception& e) {
    logSystem(tr("Error procesando mensaje del servidor: %1").arg(QString::fromUtf8(e.what())));
  }
}

}  // namespace templar::client
