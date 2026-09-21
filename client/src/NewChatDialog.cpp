#include "NewChatDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace templar::client {

NewChatDialog::NewChatDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Nuevo chat"));
  setModal(true);

  auto* layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel(tr("Usuario:")));
  userEdit_ = new QLineEdit();
  userEdit_->setObjectName("newChatUserEdit");
  layout->addWidget(userEdit_);

  auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttonBox->setObjectName("newChatDialogButtons");
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttonBox);
}

QString NewChatDialog::resultUsername() const { return userEdit_->text().trimmed(); }

}  // namespace templar::client
