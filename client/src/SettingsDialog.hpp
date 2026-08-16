#pragma once

#include <QDialog>

#include "Theme.hpp"

class QPushButton;
class QVBoxLayout;

namespace templar::client {

// Dialogo de ajustes puramente esteticos: colores del tema y tipografia. No
// toca la cuenta ni la conexion -- eso vive en la pantalla de login.
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(const Theme& current, QWidget* parent = nullptr);

  Theme resultTheme() const { return theme_; }

 private:
  QPushButton* addColorRow(QVBoxLayout* layout, const QString& label, QColor Theme::*field);
  void pickColor(QColor Theme::*field, QPushButton* swatch);
  void applySwatchColor(QPushButton* swatch, const QColor& color);
  void restoreDefaults();

  Theme theme_;

  QPushButton* backgroundSwatch_;
  QPushButton* foregroundSwatch_;
  QPushButton* accentSwatch_;
  QPushButton* ownMessageSwatch_;
  QPushButton* peerMessageSwatch_;
  QPushButton* systemMessageSwatch_;
};

}  // namespace templar::client
