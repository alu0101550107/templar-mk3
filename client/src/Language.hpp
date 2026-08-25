#pragma once

#include <QLocale>
#include <QString>

namespace templar {

// Idioma de la interfaz, persistido con QSettings -- independiente de la
// cuenta, mismo criterio que Theme (ver Theme.hpp). El codigo fuente esta
// escrito con tr()/qsTr() en espanol, asi que "es" nunca necesita ningun
// traductor instalado: sin traductor la UI ya se ve en espanol. Cambiar de
// idioma requiere reiniciar la app -- esta UI no viene de Qt Designer (sin
// retranslateUi() automatico) y habria que refrescar a mano cada mensaje/
// modelo ya construido, así que se prefiere reiniciar en vez de duplicar esa
// lógica.
enum class Language { Spanish, English };

QString languageCode(Language lang);  // "es" / "en" -- usado como sufijo del .qm
Language languageFromCode(const QString& code);
QLocale languageLocale(Language lang);  // para fechas (MainWindow::dateDividerHtml, etc.)
QString languageDisplayName(Language lang);  // "Español" / "English", para el selector

Language currentLanguage();   // de QSettings; Spanish si no hay nada guardado
void setLanguage(Language lang);  // a QSettings -- efectivo en el proximo arranque

}  // namespace templar
