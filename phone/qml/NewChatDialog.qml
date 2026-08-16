import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Equivalente movil de newChatPeerEdit_/newChatButton_ en el cliente de
// escritorio (client/src/MainWindow.cpp::onNewChatClicked): solo pide un
// nombre de usuario y lo anade a la lista, sin validar contra el servidor
// que exista -- la comprobacion real llega al mandar el primer mensaje.
Dialog {
    id: dialog
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
        text: "Nuevo chat"
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
            text: "Crear"
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
        if (name.length > 0) controller.startChat(name)
    }

    ColumnLayout {
        width: dialog.availableWidth

        TemplarTextField {
            id: usernameField
            placeholderText: "Usuario"
            Layout.fillWidth: true
            onAccepted: dialog.accept()
        }
    }
}
