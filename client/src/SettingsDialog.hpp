#pragma once

#include <QDialog>

#include "Language.hpp"
#include "Theme.hpp"
#include "UpdateChecker.hpp"

class QPushButton;
class QVBoxLayout;
class QComboBox;

namespace templar::client {

// Dialogo de ajustes: colores del tema, tipografia, idioma de la interfaz,
// y version instalada. No toca la cuenta ni la conexion -- eso vive en la
// pantalla de login.
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  // updateChecker: la MISMA instancia que MainWindow ya comprobo al
  // arrancar (ver MainWindow::updateChecker_) -- solo se lee aqui para
  // mostrar el enlace, este dialogo nunca dispara una comprobacion de red
  // por su cuenta.
  explicit SettingsDialog(const Theme& current, const templar::UpdateChecker& updateChecker,
                          QWidget* parent = nullptr);

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
