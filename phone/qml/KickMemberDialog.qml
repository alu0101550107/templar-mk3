import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Equivalente movil de MainWindow::onKickClicked en el escritorio
// (QInputDialog::getItem ahi, una lista de RadioButton aqui): eligir a
// quien expulsar de entre los miembros actuales del grupo, sin contarme a
// mi mismo -- mismo filtro que `candidates` en el escritorio.
Dialog {
    id: dialog
    property string groupKey: ""
    property var membersModel: []
    property string selectedUsername: ""

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
        text: "Expulsar miembro"
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
            text: "Expulsar"
            Layout.fillWidth: true
            Layout.margins: 8
            enabled: dialog.selectedUsername.length > 0
            onClicked: dialog.accept()
        }
    }

    onOpened: {
        selectedUsername = ""
        membersModel = controller.groupMembers(groupKey).filter(function (u) {
            return u !== controller.username
        })
    }

    onAccepted: {
        if (selectedUsername.length > 0) controller.kickFromGroup(groupKey, selectedUsername)
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 4

        ButtonGroup { id: memberGroup }

        Repeater {
            model: dialog.membersModel

            delegate: RadioButton {
                id: radio
                Layout.fillWidth: true
                text: modelData
                ButtonGroup.group: memberGroup
                onCheckedChanged: if (checked) dialog.selectedUsername = modelData

                contentItem: Label {
                    text: modelData
                    color: theme.foreground
                    font.family: "JetBrains Mono"
                    leftPadding: radio.indicator.width + 8
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Label {
            text: "(no hay otros miembros que expulsar)"
            color: "#666666"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            visible: dialog.membersModel.length === 0
        }
    }
}
