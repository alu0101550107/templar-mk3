#include "ChatHistoryModel.hpp"

#include <QDateTime>
#include <QLocale>

#include "Language.hpp"

namespace templar::phone {

ChatHistoryModel::ChatHistoryModel(QObject* parent) : QAbstractListModel(parent) {}

int ChatHistoryModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(lines_.size());
}

QVariant ChatHistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(lines_.size())) {
    return {};
  }
  const ChatLine& line = lines_[static_cast<size_t>(index.row())];
  switch (role) {
    case KindRole:
      return line.kind;
    case WhoRole:
      return line.who;
    case TextRole:
      return line.text;
    case TimestampRole:
      return line.timestamp;
    case RawHtmlRole:
      return line.rawHtml;
    case DateSectionRole: {
      if (line.timestamp <= 0) return QString();
      QDate date = QDateTime::fromSecsSinceEpoch(line.timestamp).date();
      QLocale locale = templar::languageLocale(templar::currentLanguage());
      return locale.toString(date, locale.dateFormat(QLocale::LongFormat));
    }
    default:
      return {};
  }
}

QHash<int, QByteArray> ChatHistoryModel::roleNames() const {
  return {
      {KindRole, "kind"},
      {WhoRole, "who"},
      {TextRole, "text"},
      {TimestampRole, "timestamp"},
      {RawHtmlRole, "rawHtml"},
      {DateSectionRole, "dateSection"},
  };
}

void ChatHistoryModel::setLines(const std::vector<ChatLine>& lines) {
  beginResetModel();
  lines_ = lines;
  endResetModel();
}

void ChatHistoryModel::append(const ChatLine& line) {
  int row = static_cast<int>(lines_.size());
  beginInsertRows(QModelIndex(), row, row);
  lines_.push_back(line);
  endInsertRows();
}

QVariantList ChatHistoryModel::findMatches(const QString& query) const {
  QVariantList result;
  if (query.isEmpty()) return result;
  for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
    if (lines_[static_cast<size_t>(i)].text.contains(query, Qt::CaseInsensitive)) result.append(i);
  }
  return result;
}

}  // namespace templar::phone
