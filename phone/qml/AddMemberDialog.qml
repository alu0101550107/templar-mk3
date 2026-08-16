import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Equivalente movil de MainWindow::onAddMemberClicked en el escritorio
// (QInputDialog::getText ahi, un solo TextField aqui). `groupKey` lo fija
// ChatPage.qml justo antes de abrir el dialogo -- no valida contra el
// servidor que el usuario exista ni que ya sea miembro, igual que el
// escritorio: el error real (si lo hay) llega como GroupErr.
Dialog {
    id: dialog
    property string groupKey: ""

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 300, 300)

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    header: Label {
        text: "Anadir miembro"
        color: theme.accent
        font.bold: true
        font.family: "JetBrains Mono"
        padding: 12
    }

    footer: RowLayout {
        width: dialog.width
        spacing: 8

        TemplarButton {
            text: "Cancelar"
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: dialog.reject()
        }
        TemplarButton {
            text: "Invitar"
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: dialog.accept()
        }
    }

    onOpened: {
        usernameField.text = ""
        usernameField.forceActiveFocus()
    }

    onAccepted: {
        var name = usernameField.text.trim()
        if (name.length > 0) controller.inviteToGroup(groupKey, name)
    }

    ColumnLayout {
        width: dialog.availableWidth

        TemplarTextField {
            id: usernameField
            placeholderText: "Usuario a invitar"
            Layout.fillWidth: true
            onAccepted: dialog.accept()
        }
    }
}
