#include "CreateGroupDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace templar::client {

CreateGroupDialog::CreateGroupDialog(const QStringList& contacts, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Crear grupo"));
  setModal(true);

  auto* layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel(tr("Nombre del grupo:")));
  nameEdit_ = new QLineEdit();
  nameEdit_->setObjectName("groupNameEdit");
  layout->addWidget(nameEdit_);

  layout->addWidget(new QLabel(tr("Invitar a:")));
  contactsList_ = new QListWidget();
  contactsList_->setObjectName("groupContactsList");
  for (const QString& contact : contacts) {
    auto* item = new QListWidgetItem(contact, contactsList_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
  }
  layout->addWidget(contactsList_, /*stretch=*/1);

  auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttonBox->setObjectName("groupDialogButtons");
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttonBox);
}

QString CreateGroupDialog::resultName() const { return nameEdit_->text().trimmed(); }

QStringList CreateGroupDialog::resultSelectedContacts() const {
  QStringList out;
  for (int i = 0; i < contactsList_->count(); ++i) {
    QListWidgetItem* item = contactsList_->item(i);
    if (item->checkState() == Qt::Checked) out << item->text();
  }
  return out;
}

}  // namespace templar::client
