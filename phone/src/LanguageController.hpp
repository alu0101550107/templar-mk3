#pragma once

#include <QObject>
#include <QString>

namespace templar::phone {

// Expone Language.hpp (client/src/Language.hpp) a QML -- mismo criterio que
// ThemeController para Theme. A diferencia del tema, el idioma no cambia en
// caliente (ver el comentario de templar::Language): currentCode/
// currentDisplayName reflejan el idioma con el que ARRANCO esta ejecucion
// (de ahi CONSTANT, sin NOTIFY) y setLanguage() solo persiste la eleccion
// para el proximo arranque.
class LanguageController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentCode READ currentCode CONSTANT)
  Q_PROPERTY(QString currentDisplayName READ currentDisplayName CONSTANT)

 public:
  explicit LanguageController(QObject* parent = nullptr);

  QString currentCode() const;
  QString currentDisplayName() const;

  Q_INVOKABLE void setLanguage(const QString& code);
  Q_INVOKABLE QString displayNameForCode(const QString& code) const;
};

}  // namespace templar::phone
