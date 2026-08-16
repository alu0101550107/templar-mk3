#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

namespace templar::phone {

// Modelo para el ListView de ChatListPage.qml -- equivalente movil de
// conversations_/listedKeys_ en el cliente de escritorio
// (client/src/MainWindow.cpp), pero como QAbstractListModel de verdad en
// vez de manipular items de un QListWidget a mano.
//
// `key` es el username del peer para un chat 1-a-1, o el id opaco del
// grupo para uno de grupo -- mismo criterio de clave unica que
// conversation_key en LocalStore/MainWindow del escritorio.
class ConversationListModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Roles {
    KeyRole = Qt::UserRole + 1,
    NameRole,
    IsGroupRole,
    OnlineRole,
    UnreadRole,
  };

  explicit ConversationListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Anade la entrada si no existia, o actualiza el nombre si ya existia
  // (p.ej. el nombre de un grupo puede cambiar de un login a otro).
  // Devuelve true si era una entrada nueva.
  bool upsert(const QString& key, const QString& name, bool isGroup);

  bool contains(const QString& key) const;

  void setOnline(const QString& key, bool online);
  // Contador de mensajes sin leer de una conversacion -- equivalente movil
  // de updateSidebarUnreadStyle en el escritorio (aqui solo el numero, el
  // formato "Nombre (N)" y la negrita se aplican en QML). No hace nada si
  // `key` todavia no esta listada (p.ej. un grupo cuyo MyGroups no ha
  // llegado todavia) -- ver el comentario de markUnread en
  // ClientController.
  void setUnreadCount(const QString& key, int count);
  // A diferencia de adminOf()/membersOf() (llamadas solo desde
  // ClientController), esta se llama directo desde QML (ChatPage.qml, para
  // el punto de presencia junto al nombre en la cabecera) -- de ahi el
  // Q_INVOKABLE.
  Q_INVOKABLE bool isOnline(const QString& key) const;

  // Quita todas las entradas de grupo (no las de chats 1-a-1) -- se llama
  // al recibir un MyGroups fresco al iniciar sesion, para que un grupo del
  // que ya no soy miembro (me expulsaron, sali, o se borro mientras estaba
  // desconectado) desaparezca de la lista igual que en el escritorio.
  void removeGroups();

  // Vacia el modelo entero (grupos Y chats 1-a-1) -- se llama al
  // desconectar, para que los chats de una cuenta no se queden pegados en
  // la lista al iniciar sesion despues con una cuenta distinta en la misma
  // app (ConversationListModel vive en ClientController, que sobrevive a
  // un logout/login sin reiniciar el proceso).
  void clear();

  // Admin y lista de miembros de un grupo -- vacio/[] para un chat 1-a-1.
  // Equivalente movil de GroupInfo::adminUsername/members en
  // MainWindow.hpp del escritorio, guardado dentro de la misma Entry en vez
  // de un mapa aparte porque el modelo ya indexa por key.
  void setGroupInfo(const QString& key, const QString& admin, const QStringList& members);
  void setAdmin(const QString& key, const QString& admin);
  void addMember(const QString& key, const QString& username);
  void removeMember(const QString& key, const QString& username);
  // Quita una unica entrada (grupo del que me expulsaron o al que sali) --
  // a diferencia de removeGroups(), no toca el resto de grupos.
  void removeEntry(const QString& key);
  QString adminOf(const QString& key) const;
  QStringList membersOf(const QString& key) const;
  // Nombre visible de una entrada (nombre de grupo, o el propio username
  // para un chat 1-a-1) -- usado por las notificaciones nativas de Android
  // para mostrar el nombre del grupo en vez de su id opaco (ver
  // ClientController::showSystemNotification).
  QString nameOf(const QString& key) const;
  // false tanto si `key` es un chat 1-a-1 como si es una entrada
  // desconocida -- mismo criterio que isOnline() para "no aplica".
  bool isGroupChat(const QString& key) const;

 private:
  struct Entry {
    QString key;
    QString name;
    bool isGroup = false;
    bool online = false;
    int unreadCount = 0;
    QString admin;
    QStringList members;
  };

  int indexOfKey(const QString& key) const;

  std::vector<Entry> entries_;
};

}  // namespace templar::phone
