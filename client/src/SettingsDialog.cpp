#include "SettingsDialog.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace templar::client {

SettingsDialog::SettingsDialog(const Theme& current, QWidget* parent)
    : QDialog(parent), theme_(current) {
  setWindowTitle("Ajustes");
  setModal(true);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel("<b>Colores</b>"));

  backgroundSwatch_ = addColorRow(layout, "Fondo:", &Theme::background);
  foregroundSwatch_ = addColorRow(layout, "Texto general:", &Theme::foreground);
  accentSwatch_ = addColorRow(layout, "Bordes / botones:", &Theme::accent);
  ownMessageSwatch_ = addColorRow(layout, "Tu nombre en el chat:", &Theme::ownMessage);
  peerMessageSwatch_ = addColorRow(layout, "Nombre del interlocutor:", &Theme::peerMessage);
  systemMessageSwatch_ = addColorRow(layout, "Mensajes de sistema:", &Theme::systemMessage);

  auto* restoreButton = new QPushButton("Restaurar valores por defecto");
  connect(restoreButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
  layout->addWidget(restoreButton);

  auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttonBox);
}

QPushButton* SettingsDialog::addColorRow(QVBoxLayout* layout, const QString& label,
                                         QColor Theme::*field) {
  auto* row = new QHBoxLayout();
  row->addWidget(new QLabel(label));

  auto* swatch = new QPushButton();
  swatch->setFixedSize(48, 24);
  applySwatchColor(swatch, theme_.*field);
  connect(swatch, &QPushButton::clicked, this, [this, field, swatch] { pickColor(field, swatch); });

  row->addWidget(swatch);
  row->addStretch(1);
  layout->addLayout(row);
  return swatch;
}

void SettingsDialog::pickColor(QColor Theme::*field, QPushButton* swatch) {
  QColor chosen = QColorDialog::getColor(theme_.*field, this, "Elegir color");
  if (!chosen.isValid()) return;  // el usuario cancelo el selector
  theme_.*field = chosen;
  applySwatchColor(swatch, chosen);
}

void SettingsDialog::applySwatchColor(QPushButton* swatch, const QColor& color) {
  swatch->setStyleSheet(
      QString("background-color: %1; border: 1px solid #888888;").arg(color.name()));
}

void SettingsDialog::restoreDefaults() {
  theme_ = Theme::defaults();
  applySwatchColor(backgroundSwatch_, theme_.background);
  applySwatchColor(foregroundSwatch_, theme_.foreground);
  applySwatchColor(accentSwatch_, theme_.accent);
  applySwatchColor(ownMessageSwatch_, theme_.ownMessage);
  applySwatchColor(peerMessageSwatch_, theme_.peerMessage);
  applySwatchColor(systemMessageSwatch_, theme_.systemMessage);
}

}  // namespace templar::client
