#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

namespace templar::client {

// Dialogo modal para crear un grupo: nombre + checklist de contactos
// conocidos (conversaciones 1-a-1 ya existentes) a invitar. El creador
// siempre queda como admin y unico miembro inicial en el servidor -- los
// contactos marcados aqui se invitan aparte, justo despues de crear el
// grupo (ver MainWindow::onCreateGroupClicked).
class CreateGroupDialog : public QDialog {
  Q_OBJECT

 public:
  // `contacts` son las claves de conversacion 1-a-1 ya conocidas (nunca
  // incluye grupos ni el log de sistema).
  CreateGroupDialog(const QStringList& contacts, QWidget* parent = nullptr);

  QString resultName() const;
  QStringList resultSelectedContacts() const;

 private:
  QLineEdit* nameEdit_;
  QListWidget* contactsList_;
};

}  // namespace templar::client
