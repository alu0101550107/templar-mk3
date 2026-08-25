#pragma once

#include <QDialog>

#include "Language.hpp"
#include "Theme.hpp"

class QPushButton;
class QVBoxLayout;
class QComboBox;

namespace templar::client {

// Dialogo de ajustes: colores del tema, tipografia, e idioma de la
// interfaz. No toca la cuenta ni la conexion -- eso vive en la pantalla de
// login.
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(const Theme& current, QWidget* parent = nullptr);

  Theme resultTheme() const { return theme_; }
  // Idioma elegido en el desplegable -- MainWindow compara contra
  // currentLanguage() para decidir si hace falta avisar de que hay que
  // reiniciar (ver Language.hpp: el cambio de idioma no se aplica en
  // caliente).
  Language resultLanguage() const { return language_; }

 private:
  QPushButton* addColorRow(QVBoxLayout* layout, const QString& label, QColor Theme::*field);
  void pickColor(QColor Theme::*field, QPushButton* swatch);
  void applySwatchColor(QPushButton* swatch, const QColor& color);
  void restoreDefaults();

  Theme theme_;
  Language language_;

  QPushButton* backgroundSwatch_;
  QPushButton* foregroundSwatch_;
  QPushButton* accentSwatch_;
  QPushButton* ownMessageSwatch_;
  QPushButton* peerMessageSwatch_;
  QPushButton* systemMessageSwatch_;
  QComboBox* languageCombo_;
};

}  // namespace templar::client
