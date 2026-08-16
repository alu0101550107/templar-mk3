import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Equivalente movil de CreateGroupDialog.cpp en el cliente de escritorio:
// nombre + checklist de contactos 1-a-1 ya conocidos (nunca grupos) a
// invitar. El creador queda como admin y unico miembro inicial en el
// servidor -- los marcados aqui se invitan aparte, justo despues de que el
// servidor confirme que el grupo existe (ver
// ClientController::createGroup / case GroupCreated).
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 320, 320)

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    header: Label {
        text: "Nuevo grupo"
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

    onOpened: nameField.text = ""

    onAccepted: {
        var invitees = []
        for (var i = 0; i < contactsRepeater.count; ++i) {
            var row = contactsRepeater.itemAt(i)
            if (row && row.contactChecked) invitees.push(row.contactKey)
        }
        var name = nameField.text.trim()
        if (name.length > 0) controller.createGroup(name, invitees)
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 8

        TemplarTextField {
            id: nameField
            placeholderText: "Nombre del grupo"
            Layout.fillWidth: true
        }

        Label {
            text: "Invitar (opcional):"
            color: theme.foreground
            Layout.topMargin: 4
        }

        // Solo chats 1-a-1 ya conocidos, nunca grupos -- mismo criterio que
        // `contacts` en CreateGroupDialog.hpp del escritorio.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Repeater {
                id: contactsRepeater
                model: controller.conversations

                delegate: RowLayout {
                    Layout.fillWidth: true
                    visible: !isGroup
                    height: visible ? implicitHeight : 0

                    property string contactKey: key
                    property alias contactChecked: box.checked

                    CheckBox {
                        id: box
                        Layout.fillWidth: true
                        contentItem: Label {
                            text: name
                            color: theme.foreground
                            leftPadding: box.indicator.width + 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Label {
                text: "(todavia no tienes ningun chat 1-a-1 a quien invitar)"
                color: "#666666"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                visible: contactsRepeater.count === 0
            }
        }
    }
}
