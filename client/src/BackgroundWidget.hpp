#pragma once

#include <QColor>
#include <QPixmap>
#include <QWidget>

namespace templar::client {

// Widget contenedor que pinta un color base y una imagen de fondo
// (desenfocada y atenuada) detras de los widgets hijos que se le añadan por
// layout normal. Se usa como base tanto de la pantalla de login como del
// area de chat.
class BackgroundWidget : public QWidget {
  Q_OBJECT

 public:
  explicit BackgroundWidget(QWidget* parent = nullptr);

  // Aplica desenfoque gaussiano a `source` y la deja lista para pintarse de
  // fondo: cubre todo el widget (recortando lo que sobre, como
  // "background-size: cover"), centrada, con la opacidad indicada aplicada
  // al pintar.
  void setBackgroundPixmap(const QPixmap& source, qreal blurRadius = 20.0, qreal opacity = 0.28);

  // Color solido que se pinta debajo del logo (sigue al Theme activo).
  void setBaseColor(const QColor& color);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QPixmap blurredPixmap_;
  qreal opacity_ = 0.16;
  QColor baseColor_ = QColor(8, 8, 8);
};

}  // namespace templar::client
