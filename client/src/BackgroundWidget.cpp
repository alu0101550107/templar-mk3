#include "BackgroundWidget.hpp"

#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>

namespace templar::client {

BackgroundWidget::BackgroundWidget(QWidget* parent) : QWidget(parent) {}

void BackgroundWidget::setBackgroundPixmap(const QPixmap& source, qreal blurRadius,
                                           qreal opacity) {
  opacity_ = opacity;

  if (source.isNull()) {
    blurredPixmap_ = QPixmap();
    update();
    return;
  }

  QGraphicsScene scene;
  QGraphicsPixmapItem* item = scene.addPixmap(source);
  item->setTransformationMode(Qt::SmoothTransformation);

  auto* blur = new QGraphicsBlurEffect();
  blur->setBlurRadius(blurRadius);
  blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
  item->setGraphicsEffect(blur);

  QPixmap result(source.size());
  result.fill(Qt::transparent);
  QPainter painter(&result);
  scene.render(&painter, QRectF(0, 0, source.width(), source.height()),
              QRectF(0, 0, source.width(), source.height()));
  painter.end();

  blurredPixmap_ = result;
  update();
}

void BackgroundWidget::setBaseColor(const QColor& color) {
  baseColor_ = color;
  update();
}

void BackgroundWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), baseColor_);

  if (!blurredPixmap_.isNull()) {
    // "cover": escala para cubrir todo el widget (recorta lo que sobra) en
    // vez de dejar barras negras -- asi el logo se ve grande de verdad, no
    // como una marca de agua pequena.
    QSize target = blurredPixmap_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
    QRect destRect(QPoint(0, 0), target);
    destRect.moveCenter(rect().center());

    painter.setOpacity(opacity_);
    painter.drawPixmap(destRect, blurredPixmap_);
    painter.setOpacity(1.0);
  }
}

}  // namespace templar::client
