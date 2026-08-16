#include "Theme.hpp"

#include <QSettings>

namespace templar::client {

Theme Theme::defaults() {
  Theme t;
  t.background = QColor("#0a0a0a");
  t.foreground = QColor("#d8d8d8");
  t.accent = QColor("#33ff33");
  t.ownMessage = QColor("#33ff66");
  t.peerMessage = QColor("#4da6ff");
  t.systemMessage = QColor("#888888");
  t.fontFamily = "JetBrains Mono";
  return t;
}

Theme Theme::load() {
  QSettings settings;
  Theme d = defaults();
  Theme t;
  settings.beginGroup("theme");
  t.background = QColor(settings.value("background", d.background.name()).toString());
  t.foreground = QColor(settings.value("foreground", d.foreground.name()).toString());
  t.accent = QColor(settings.value("accent", d.accent.name()).toString());
  t.ownMessage = QColor(settings.value("ownMessage", d.ownMessage.name()).toString());
  t.peerMessage = QColor(settings.value("peerMessage", d.peerMessage.name()).toString());
  t.systemMessage = QColor(settings.value("systemMessage", d.systemMessage.name()).toString());
  t.fontFamily = settings.value("fontFamily", d.fontFamily).toString();
  settings.endGroup();

  // Si algun color guardado esta corrupto/vacio, no dejar la UI con colores
  // invalidos -- cae de vuelta al valor por defecto de ese campo.
  if (!t.background.isValid()) t.background = d.background;
  if (!t.foreground.isValid()) t.foreground = d.foreground;
  if (!t.accent.isValid()) t.accent = d.accent;
  if (!t.ownMessage.isValid()) t.ownMessage = d.ownMessage;
  if (!t.peerMessage.isValid()) t.peerMessage = d.peerMessage;
  if (!t.systemMessage.isValid()) t.systemMessage = d.systemMessage;
  if (t.fontFamily.isEmpty()) t.fontFamily = d.fontFamily;

  return t;
}

void Theme::save() const {
  QSettings settings;
  settings.beginGroup("theme");
  settings.setValue("background", background.name());
  settings.setValue("foreground", foreground.name());
  settings.setValue("accent", accent.name());
  settings.setValue("ownMessage", ownMessage.name());
  settings.setValue("peerMessage", peerMessage.name());
  settings.setValue("systemMessage", systemMessage.name());
  settings.setValue("fontFamily", fontFamily);
  settings.endGroup();
}

QString Theme::toQss() const {
  return QString(
             "QWidget {"
             "  background-color: %1;"
             "  color: %2;"
             "  font-family: \"%4\", monospace;"
             "  selection-background-color: %3;"
             "  selection-color: %1;"
             "}"
             "QLineEdit, QTextEdit, QListWidget {"
             "  background-color: rgba(0, 0, 0, 140);"
             "  color: %2;"
             "  border: 1px solid %3;"
             "  border-radius: 2px;"
             "  padding: 4px;"
             "}"
             "QLineEdit:disabled, QTextEdit:disabled, QListWidget:disabled {"
             "  border-color: #444444;"
             "  color: #666666;"
             "}"
             "QPushButton {"
             "  background-color: transparent;"
             "  color: %3;"
             "  border: 1px solid %3;"
             "  border-radius: 2px;"
             "  padding: 5px 12px;"
             "}"
             "QPushButton:hover:!disabled {"
             "  background-color: %3;"
             "  color: %1;"
             "}"
             "QPushButton:pressed:!disabled {"
             "  background-color: %2;"
             "  color: %1;"
             "}"
             "QPushButton:disabled {"
             "  color: #555555;"
             "  border-color: #444444;"
             "}"
             "QListWidget::item {"
             "  padding: 4px;"
             "}"
             "QListWidget::item:selected {"
             "  background-color: %3;"
             "  color: %1;"
             "}"
             "QLabel {"
             "  background: transparent;"
             "  border: none;"
             "}"
             "QSplitter::handle {"
             "  background-color: %3;"
             "}")
      .arg(background.name(), foreground.name(), accent.name(), fontFamily);
}

}  // namespace templar::client
