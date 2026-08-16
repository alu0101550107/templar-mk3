import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Equivalente movil de SettingsDialog.cpp en el cliente de escritorio:
// mismos seis colores editables (fondo, texto general, acento, y los tres
// de mensajes), mismo criterio de "borrador propio, no se aplica a la app
// hasta pulsar Guardar" -- theme (ThemeController) no se toca hasta
// onAccepted, y "Cancelar" simplemente descarta el borrador sin tocar nada.
// Fondo/cabecera/pie propios (no standardButtons) para no depender de como
// los dibuje el estilo activo -- mismo motivo que TemplarColorPicker.qml.
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 360, 360)

    property color draftBackground: theme.background
    property color draftForeground: theme.foreground
    property color draftAccent: theme.accent
    property color draftOwnMessage: theme.ownMessage
    property color draftPeerMessage: theme.peerMessage
    property color draftSystemMessage: theme.systemMessage

    // Se re-copia del tema real cada vez que se abre -- por si la vez
    // anterior se cancelo a mitad de editar, no debe arrastrar ese borrador
    // descartado a la siguiente apertura.
    onOpened: {
        draftBackground = theme.background
        draftForeground = theme.foreground
        draftAccent = theme.accent
        draftOwnMessage = theme.ownMessage
        draftPeerMessage = theme.peerMessage
        draftSystemMessage = theme.systemMessage
    }

    onAccepted: {
        theme.background = draftBackground
        theme.foreground = draftForeground
        theme.accent = draftAccent
        theme.ownMessage = draftOwnMessage
        theme.peerMessage = draftPeerMessage
        theme.systemMessage = draftSystemMessage
        theme.save()
    }

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    header: Label {
        text: "Ajustes"
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
            text: "Guardar"
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: dialog.accept()
        }
    }

    // Un unico TemplarColorPicker compartido por las seis filas --
    // pickerTarget dice a cual de los draftXxx de arriba escribir cuando se
    // acepte (el propio picker no sabe nada de "campos", solo entrega un
    // color).
    TemplarColorPicker {
        id: colorPicker
        property string pickerTarget: ""
        onAccepted: {
            switch (colorPicker.pickerTarget) {
            case "background": dialog.draftBackground = colorPicker.pickedColor; break
            case "foreground": dialog.draftForeground = colorPicker.pickedColor; break
            case "accent": dialog.draftAccent = colorPicker.pickedColor; break
            case "ownMessage": dialog.draftOwnMessage = colorPicker.pickedColor; break
            case "peerMessage": dialog.draftPeerMessage = colorPicker.pickedColor; break
            case "systemMessage": dialog.draftSystemMessage = colorPicker.pickedColor; break
            }
        }
    }

    function pickColor(field, current) {
        colorPicker.pickerTarget = field
        colorPicker.pickedColor = current
        colorPicker.open()
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Fondo:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftBackground
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("background", dialog.draftBackground) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Texto general:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftForeground
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("foreground", dialog.draftForeground) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Bordes / botones:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftAccent
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("accent", dialog.draftAccent) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Tu nombre en el chat:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftOwnMessage
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("ownMessage", dialog.draftOwnMessage) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Nombre del interlocutor:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftPeerMessage
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("peerMessage", dialog.draftPeerMessage) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Mensajes de sistema:"; color: theme.foreground; Layout.fillWidth: true }
            Rectangle {
                width: 48; height: 24
                color: dialog.draftSystemMessage
                border.color: theme.accent
                border.width: 1
                radius: 2
                MouseArea { anchors.fill: parent; onClicked: dialog.pickColor("systemMessage", dialog.draftSystemMessage) }
            }
        }

        TemplarButton {
            text: "Restaurar valores por defecto"
            Layout.fillWidth: true
            Layout.topMargin: 8
            onClicked: {
                dialog.draftBackground = theme.defaultBackground
                dialog.draftForeground = theme.defaultForeground
                dialog.draftAccent = theme.defaultAccent
                dialog.draftOwnMessage = theme.defaultOwnMessage
                dialog.draftPeerMessage = theme.defaultPeerMessage
                dialog.draftSystemMessage = theme.defaultSystemMessage
            }
        }
    }
}
