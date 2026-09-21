#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

namespace templar::client {

// Dialogo modal para abrir un chat 1-a-1: solo pide el nombre de usuario.
// Equivalente de escritorio de phone/qml/NewChatDialog.qml -- tampoco valida
// contra el servidor que el usuario exista (la comprobacion real llega al
// mandar el primer mensaje).
class NewChatDialog : public QDialog {
  Q_OBJECT

 public:
  explicit NewChatDialog(QWidget* parent = nullptr);

  QString resultUsername() const;

 private:
  QLineEdit* userEdit_;
};

}  // namespace templar::client
