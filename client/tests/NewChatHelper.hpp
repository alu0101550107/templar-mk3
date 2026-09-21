#pragma once

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTimer>

// Abre el NewChatDialog real de `window` (pulsando su boton "Nuevo chat") y lo
// rellena/acepta desde un QTimer, porque exec() bloquea hasta que se cierra.
inline void startChatViaDialog(QWidget* window, const QString& peer) {
  QTimer::singleShot(200, [peer] {
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* d = qobject_cast<QDialog*>(w);
      if (!d) continue;
      auto* edit = d->findChild<QLineEdit*>("newChatUserEdit");
      auto* buttons = d->findChild<QDialogButtonBox*>("newChatDialogButtons");
      if (!edit || !buttons) continue;
      edit->setText(peer);
      buttons->button(QDialogButtonBox::Ok)->click();
      return;
    }
  });
  window->findChild<QPushButton*>("newChatButton")->click();
}
