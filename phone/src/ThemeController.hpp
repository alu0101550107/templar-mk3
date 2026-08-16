#pragma once

#include <QColor>
#include <QObject>

#include "Theme.hpp"

namespace templar::phone {

// Envuelve templar::client::Theme (client/src/Theme.hpp) para exponerlo a
// QML como propiedades reactivas -- la propia Theme es reutilizable tal
// cual (QColor + QSettings, nada de Qt Widgets salvo toQss(), que aqui no
// se usa: cada componente QML lee sus colores directamente en vez de una
// hoja de estilos generada).
//
// Instancia UNICA expuesta globalmente desde main.cpp (contexto "theme"),
// igual que "controller" -- todas las paginas y componentes Templar*.qml
// leen de aqui en vez de tener colores fijos, para que Ajustes pueda
// cambiarlos en caliente en toda la app a la vez.
class ThemeController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)
  Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
  Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
  Q_PROPERTY(QColor ownMessage READ ownMessage WRITE setOwnMessage NOTIFY ownMessageChanged)
  Q_PROPERTY(QColor peerMessage READ peerMessage WRITE setPeerMessage NOTIFY peerMessageChanged)
  Q_PROPERTY(
      QColor systemMessage READ systemMessage WRITE setSystemMessage NOTIFY systemMessageChanged)
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontFamilyChanged)

  // Valores por defecto, de solo lectura -- para el boton "Restaurar
  // valores por defecto" del dialogo de ajustes, sin duplicar los literales
  // de Theme::defaults() en QML.
  Q_PROPERTY(QColor defaultBackground READ defaultBackground CONSTANT)
  Q_PROPERTY(QColor defaultForeground READ defaultForeground CONSTANT)
  Q_PROPERTY(QColor defaultAccent READ defaultAccent CONSTANT)
  Q_PROPERTY(QColor defaultOwnMessage READ defaultOwnMessage CONSTANT)
  Q_PROPERTY(QColor defaultPeerMessage READ defaultPeerMessage CONSTANT)
  Q_PROPERTY(QColor defaultSystemMessage READ defaultSystemMessage CONSTANT)

 public:
  explicit ThemeController(QObject* parent = nullptr);

  QColor background() const { return theme_.background; }
  QColor foreground() const { return theme_.foreground; }
  QColor accent() const { return theme_.accent; }
  QColor ownMessage() const { return theme_.ownMessage; }
  QColor peerMessage() const { return theme_.peerMessage; }
  QColor systemMessage() const { return theme_.systemMessage; }
  QString fontFamily() const { return theme_.fontFamily; }

  void setBackground(const QColor& value);
  void setForeground(const QColor& value);
  void setAccent(const QColor& value);
  void setOwnMessage(const QColor& value);
  void setPeerMessage(const QColor& value);
  void setSystemMessage(const QColor& value);

  QColor defaultBackground() const { return defaults_.background; }
  QColor defaultForeground() const { return defaults_.foreground; }
  QColor defaultAccent() const { return defaults_.accent; }
  QColor defaultOwnMessage() const { return defaults_.ownMessage; }
  QColor defaultPeerMessage() const { return defaults_.peerMessage; }
  QColor defaultSystemMessage() const { return defaults_.systemMessage; }

  // Persiste el estado actual a QSettings -- llamar solo al confirmar el
  // dialogo de ajustes (Guardar), no en cada cambio de color individual,
  // igual que MainWindow::onSettingsClicked en el cliente de escritorio.
  Q_INVOKABLE void save();

 signals:
  void backgroundChanged();
  void foregroundChanged();
  void accentChanged();
  void ownMessageChanged();
  void peerMessageChanged();
  void systemMessageChanged();
  void fontFamilyChanged();

 private:
  templar::client::Theme theme_;
  const templar::client::Theme defaults_;
};

}  // namespace templar::phone
