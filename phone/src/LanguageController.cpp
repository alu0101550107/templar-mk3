#include "LanguageController.hpp"

#include "Language.hpp"

namespace templar::phone {

LanguageController::LanguageController(QObject* parent) : QObject(parent) {}

QString LanguageController::currentCode() const { return templar::languageCode(templar::currentLanguage()); }

QString LanguageController::currentDisplayName() const {
  return templar::languageDisplayName(templar::currentLanguage());
}

void LanguageController::setLanguage(const QString& code) {
  templar::setLanguage(templar::languageFromCode(code));
}

QString LanguageController::displayNameForCode(const QString& code) const {
  return templar::languageDisplayName(templar::languageFromCode(code));
}

}  // namespace templar::phone
