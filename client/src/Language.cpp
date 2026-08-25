#include "Language.hpp"

#include <QSettings>

namespace templar {

QString languageCode(Language lang) { return lang == Language::English ? "en" : "es"; }

Language languageFromCode(const QString& code) {
  return code == "en" ? Language::English : Language::Spanish;
}

QLocale languageLocale(Language lang) {
  return lang == Language::English ? QLocale(QLocale::English) : QLocale(QLocale::Spanish);
}

QString languageDisplayName(Language lang) {
  return lang == Language::English ? QStringLiteral("English") : QStringLiteral("Español");
}

Language currentLanguage() {
  return languageFromCode(QSettings().value("language", "es").toString());
}

void setLanguage(Language lang) { QSettings().setValue("language", languageCode(lang)); }

}  // namespace templar
