#include "ConversationListModel.hpp"

namespace templar::phone {

ConversationListModel::ConversationListModel(QObject* parent) : QAbstractListModel(parent) {}

int ConversationListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(entries_.size());
}

QVariant ConversationListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size())) {
    return {};
  }
  const Entry& e = entries_[static_cast<size_t>(index.row())];
  switch (role) {
    case KeyRole:
      return e.key;
    case NameRole:
      return e.name;
    case IsGroupRole:
      return e.isGroup;
    case OnlineRole:
      return e.online;
    case UnreadRole:
      return e.unreadCount;
    default:
      return {};
  }
}

QHash<int, QByteArray> ConversationListModel::roleNames() const {
  return {
      {KeyRole, "key"},
      {NameRole, "name"},
      {IsGroupRole, "isGroup"},
      {OnlineRole, "online"},
      {UnreadRole, "unread"},
  };
}

int ConversationListModel::indexOfKey(const QString& key) const {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].key == key) return static_cast<int>(i);
  }
  return -1;
}

bool ConversationListModel::contains(const QString& key) const { return indexOfKey(key) >= 0; }

bool ConversationListModel::upsert(const QString& key, const QString& name, bool isGroup) {
  int idx = indexOfKey(key);
  if (idx >= 0) {
    Entry& e = entries_[static_cast<size_t>(idx)];
    if (e.name != name) {
      e.name = name;
      QModelIndex mi = index(idx);
      emit dataChanged(mi, mi, {NameRole});
    }
    return false;
  }

  int row = static_cast<int>(entries_.size());
  beginInsertRows(QModelIndex(), row, row);
  Entry e;
  e.key = key;
  e.name = name;
  e.isGroup = isGroup;
  entries_.push_back(e);
  endInsertRows();
  return true;
}

void ConversationListModel::setOnline(const QString& key, bool online) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  Entry& e = entries_[static_cast<size_t>(idx)];
  if (e.online == online) return;
  e.online = online;
  QModelIndex mi = index(idx);
  emit dataChanged(mi, mi, {OnlineRole});
}

bool ConversationListModel::isOnline(const QString& key) const {
  int idx = indexOfKey(key);
  if (idx < 0) return false;
  return entries_[static_cast<size_t>(idx)].online;
}

void ConversationListModel::setUnreadCount(const QString& key, int count) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  Entry& e = entries_[static_cast<size_t>(idx)];
  if (e.unreadCount == count) return;
  e.unreadCount = count;
  QModelIndex mi = index(idx);
  emit dataChanged(mi, mi, {UnreadRole});
}

void ConversationListModel::removeGroups() {
  for (int i = static_cast<int>(entries_.size()) - 1; i >= 0; --i) {
    if (entries_[static_cast<size_t>(i)].isGroup) {
      beginRemoveRows(QModelIndex(), i, i);
      entries_.erase(entries_.begin() + i);
      endRemoveRows();
    }
  }
}

void ConversationListModel::clear() {
  if (entries_.empty()) return;
  beginResetModel();
  entries_.clear();
  endResetModel();
}

void ConversationListModel::setGroupInfo(const QString& key, const QString& admin,
                                         const QStringList& members) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  entries_[static_cast<size_t>(idx)].admin = admin;
  entries_[static_cast<size_t>(idx)].members = members;
}

void ConversationListModel::setAdmin(const QString& key, const QString& admin) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  entries_[static_cast<size_t>(idx)].admin = admin;
}

void ConversationListModel::addMember(const QString& key, const QString& username) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  QStringList& members = entries_[static_cast<size_t>(idx)].members;
  if (!members.contains(username)) members.push_back(username);
}

void ConversationListModel::removeMember(const QString& key, const QString& username) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  entries_[static_cast<size_t>(idx)].members.removeAll(username);
}

void ConversationListModel::removeEntry(const QString& key) {
  int idx = indexOfKey(key);
  if (idx < 0) return;
  beginRemoveRows(QModelIndex(), idx, idx);
  entries_.erase(entries_.begin() + idx);
  endRemoveRows();
}

QString ConversationListModel::adminOf(const QString& key) const {
  int idx = indexOfKey(key);
  if (idx < 0) return {};
  return entries_[static_cast<size_t>(idx)].admin;
}

QStringList ConversationListModel::membersOf(const QString& key) const {
  int idx = indexOfKey(key);
  if (idx < 0) return {};
  return entries_[static_cast<size_t>(idx)].members;
}

QString ConversationListModel::nameOf(const QString& key) const {
  int idx = indexOfKey(key);
  if (idx < 0) return key;
  return entries_[static_cast<size_t>(idx)].name;
}

bool ConversationListModel::isGroupChat(const QString& key) const {
  int idx = indexOfKey(key);
  if (idx < 0) return false;
  return entries_[static_cast<size_t>(idx)].isGroup;
}

}  // namespace templar::phone
