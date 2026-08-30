#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <vector>

namespace templar::phone {

// Una linea de historial ya en memoria, lista para mostrar -- equivalente
// movil de ChatLine en MainWindow.hpp del escritorio. `kind` es el mismo
// entero opaco que usa LocalStore::HistoryLine (0=sistema, 1=propio,
// 2=del interlocutor); ChatPage.qml lo traduce a color.
struct ChatLine {
  int kind = 0;
  QString who;
  QString text;
  qint64 timestamp = 0;
  // Casi siempre false: `text` se escapa como HTML en ChatPage.qml antes
  // de mostrarse. Solo true para el enlace "Descargar" de un
  // FileBlobPointer, que generamos nosotros mismos con HTML de proposito
  // (ver ClientController::onFileBlobPointerReceived) -- nunca para nada
  // que venga de otra persona.
  bool rawHtml = false;
  // Vacios si este mensaje no responde a otro -- si no, una COPIA
  // (recortada) del mensaje original, no una referencia por id (ver el
  // comentario de MessagePayload::TextReply en client/src/MessagePayload.hpp).
  QString replyToSender;
  QString replyToText;
};

// Modelo para el ListView de ChatPage.qml -- muestra el historial de UNA
// conversacion a la vez, la que ClientController::setActiveConversation()
// haya fijado como activa. Equivalente movil de chatView_ (el QTextEdit
// que MainWindow::renderActiveConversation() rellena linea a linea), pero
// como modelo real en vez de texto formateado a mano -- el color/alineado
// de cada linea lo decide ChatPage.qml a partir de KindRole, no aqui.
class ChatHistoryModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Roles {
    KindRole = Qt::UserRole + 1,
    WhoRole,
    TextRole,
    TimestampRole,
    RawHtmlRole,
    // Fecha ya formateada ("6 de agosto de 2026", locale espanol) para
    // agrupar el ListView de ChatPage.qml con ListView.section -- cadena
    // vacia si timestamp <= 0 (mensaje de antes de que el historial
    // guardara la hora). Equivalente movil de
    // MainWindow::dateDividerHtml/appendLiveLine en el escritorio, pero
    // aqui se resuelve con el mecanismo de secciones nativo de
    // ListView en vez de insertar una linea mas a mano.
    DateSectionRole,
    ReplyToSenderRole,
    ReplyToTextRole,
  };

  explicit ChatHistoryModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Sustituye todo el contenido -- se usa al cambiar de conversacion
  // activa (ver ClientController::setActiveConversation).
  void setLines(const std::vector<ChatLine>& lines);

  // Anade una linea nueva al final -- se usa cuando llega o se manda un
  // mensaje mientras esa conversacion ya esta siendo la activa, para que
  // el ListView se actualice en caliente sin recargar todo el modelo.
  void append(const ChatLine& line);

  // Indices (0-based, en el orden actual del modelo) de las lineas cuyo
  // texto contiene `query`, insensible a mayusculas/minusculas -- sobre el
  // texto PLANO de cada linea, igual que QTextEdit::find() en el
  // escritorio busca sobre toPlainText() y no sobre el HTML ya escapado
  // que arma ChatPage.qml. Lo usa la barra de busqueda de ChatPage.qml
  // para saltar entre coincidencias sin tener que iterar el modelo linea
  // a linea desde QML (QAbstractListModel no da acceso conveniente a
  // datos por fila fuera de un delegate).
  Q_INVOKABLE QVariantList findMatches(const QString& query) const;

 private:
  std::vector<ChatLine> lines_;
};

}  // namespace templar::phone
