#pragma once

#include <QWidget>

class QTabWidget;

namespace templar::client {

// Selector de emojis nativo: no incluye ningun asset de imagen propio, solo
// inserta el caracter unicode -- la fuente de emoji instalada en el sistema
// operativo de quien lo ve es quien decide como se dibuja (igual que pegar
// un emoji a mano ya funcionaba antes de este selector). Se comporta como
// un desplegable (Qt::Popup): se cierra solo al perder el foco o al pulsar
// fuera, sin necesidad de un boton "cerrar" explicito.
class EmojiPicker : public QWidget {
  Q_OBJECT

 public:
  explicit EmojiPicker(QWidget* parent = nullptr);

 signals:
  void emojiSelected(const QString& emoji);

 private:
  void buildCategory(QTabWidget* tabs, const QString& title, const QStringList& emojis);
  // Traduce el nombre interno (clave estable, no traducida, ver kCategories
  // en el .cpp) al nombre de la pestaña -- llamadas a tr() explicitas en un
  // metodo real (no en un array a nivel de namespace) para que lupdate le
  // asigne el contexto correcto (templar::client::EmojiPicker), del que SI
  // depende QTranslator en tiempo de ejecucion.
  QString categoryTitle(const char* key) const;
};

}  // namespace templar::client
