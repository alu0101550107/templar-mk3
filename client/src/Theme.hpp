#pragma once

#include <QColor>
#include <QString>

class QSettings;

namespace templar::client {

// Paleta y tipografia configurables desde Ajustes. Es una preferencia del
// dispositivo (no de la cuenta) -- se guarda con QSettings, sin cifrar,
// separada de LocalStore (que sí guarda material sensible).
struct Theme {
  QColor background;
  QColor foreground;
  QColor accent;
  QColor ownMessage;
  QColor peerMessage;
  QColor systemMessage;
  QString fontFamily;

  static Theme defaults();

  static Theme load();  // de QSettings; si no hay nada guardado, devuelve defaults()
  void save() const;    // a QSettings

  // Genera la hoja de estilos QSS completa para aplicar a la ventana.
  QString toQss() const;
};

}  // namespace templar::client
