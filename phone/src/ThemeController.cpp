#include "ThemeController.hpp"

namespace templar::phone {

ThemeController::ThemeController(QObject* parent)
    : QObject(parent), theme_(templar::client::Theme::load()), defaults_(templar::client::Theme::defaults()) {}

void ThemeController::setBackground(const QColor& value) {
  if (theme_.background == value) return;
  theme_.background = value;
  emit backgroundChanged();
}

void ThemeController::setForeground(const QColor& value) {
  if (theme_.foreground == value) return;
  theme_.foreground = value;
  emit foregroundChanged();
}

void ThemeController::setAccent(const QColor& value) {
  if (theme_.accent == value) return;
  theme_.accent = value;
  emit accentChanged();
}

void ThemeController::setOwnMessage(const QColor& value) {
  if (theme_.ownMessage == value) return;
  theme_.ownMessage = value;
  emit ownMessageChanged();
}

void ThemeController::setPeerMessage(const QColor& value) {
  if (theme_.peerMessage == value) return;
  theme_.peerMessage = value;
  emit peerMessageChanged();
}

void ThemeController::setSystemMessage(const QColor& value) {
  if (theme_.systemMessage == value) return;
  theme_.systemMessage = value;
  emit systemMessageChanged();
}

void ThemeController::save() { theme_.save(); }

}  // namespace templar::phone
