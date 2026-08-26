#include "SettingsDialog.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace templar::client {

SettingsDialog::SettingsDialog(const Theme& current, const templar::UpdateChecker& updateChecker,
                               QWidget* parent)
    : QDialog(parent), theme_(current), language_(currentLanguage()) {
  setWindowTitle(tr("Ajustes"));
  setModal(true);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("<b>Colores</b>")));

  backgroundSwatch_ = addColorRow(layout, tr("Fondo:"), &Theme::background);
  foregroundSwatch_ = addColorRow(layout, tr("Texto general:"), &Theme::foreground);
  accentSwatch_ = addColorRow(layout, tr("Bordes / botones:"), &Theme::accent);
  ownMessageSwatch_ = addColorRow(layout, tr("Tu nombre en el chat:"), &Theme::ownMessage);
  peerMessageSwatch_ = addColorRow(layout, tr("Nombre del interlocutor:"), &Theme::peerMessage);
  systemMessageSwatch_ = addColorRow(layout, tr("Mensajes de sistema:"), &Theme::systemMessage);

  auto* restoreButton = new QPushButton(tr("Restaurar valores por defecto"));
  connect(restoreButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
  layout->addWidget(restoreButton);

  layout->addWidget(new QLabel(tr("<b>Idioma</b>")));
  auto* languageRow = new QHBoxLayout();
  languageRow->addWidget(new QLabel(tr("Idioma de la interfaz:")));
  languageCombo_ = new QComboBox();
  languageCombo_->addItem(languageDisplayName(Language::Spanish), languageCode(Language::Spanish));
  languageCombo_->addItem(languageDisplayName(Language::English), languageCode(Language::English));
  languageCombo_->setCurrentIndex(language_ == Language::English ? 1 : 0);
  connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    language_ = index == 1 ? Language::English : Language::Spanish;
  });
  languageRow->addWidget(languageCombo_, /*stretch=*/1);
  layout->addLayout(languageRow);
  auto* languageHintLabel =
      new QLabel(tr("El cambio de idioma se aplica al reiniciar la app."));
  languageHintLabel->setStyleSheet("color: #888888;");
  layout->addWidget(languageHintLabel);

  layout->addWidget(new QLabel(tr("<b>Version</b>")));
  auto* versionLabel = new QLabel();
  versionLabel->setTextFormat(Qt::RichText);
  versionLabel->setOpenExternalLinks(true);
  versionLabel->setWordWrap(true);
  if (updateChecker.updateAvailable()) {
    versionLabel->setText(
        tr("Version %1 instalada -- hay una nueva: %2. <a href='%3'>Descargar</a>")
            .arg(updateChecker.currentVersion(), updateChecker.latestVersion(),
                updateChecker.releaseUrl()));
  } else {
    versionLabel->setText(
        tr("Version %1 instalada. <a href='%2'>Ver ultima version en GitHub</a>")
            .arg(updateChecker.currentVersion(), updateChecker.releaseUrl()));
  }
  layout->addWidget(versionLabel);

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
  QColor chosen = QColorDialog::getColor(theme_.*field, this, tr("Elegir color"));
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
