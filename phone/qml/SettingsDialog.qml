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
        language.setLanguage(languageCombo.currentValue)
    }

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    header: Label {
        text: qsTr("Ajustes")
        color: theme.accent
        font.bold: true
        font.family: "JetBrains Mono"
        padding: 12
    }

    footer: RowLayout {
        width: dialog.width
        spacing: 8

        TemplarButton {
            text: qsTr("Cancelar")
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: dialog.reject()
        }
        TemplarButton {
            text: qsTr("Guardar")
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: dialog.accept()
        }
    }

    // Pide la contraseña antes de activar la huella: BiometricBridge.enable()
    // necesita cifrarla con la clave del Keystore (ver BiometricHelper.java).
    // El usuario no hace falta pedirlo aqui -- se recuerda ya en claro via
    // controller.rememberedUsername().
    Dialog {
        id: biometricPasswordDialog
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
            text: qsTr("Confirma tu contraseña")
            color: theme.accent
            font.bold: true
            font.family: "JetBrains Mono"
            wrapMode: Text.WordWrap
            padding: 12
        }

        contentItem: TemplarTextField {
            id: biometricPasswordField
            placeholderText: qsTr("Contraseña")
            echoMode: TextInput.Password
        }

        footer: RowLayout {
            width: biometricPasswordDialog.width
            spacing: 8

            TemplarButton {
                text: qsTr("Cancelar")
                Layout.fillWidth: true
                Layout.margins: 8
                onClicked: biometricPasswordDialog.reject()
            }
            TemplarButton {
                text: qsTr("Confirmar")
                Layout.fillWidth: true
                Layout.margins: 8
                onClicked: biometricPasswordDialog.accept()
            }
        }

        onAccepted: {
            biometric.enable(biometricPasswordField.text)
            biometricPasswordField.text = ""
        }
        onRejected: biometricPasswordField.text = ""
        onOpened: biometricPasswordField.forceActiveFocus()
    }

    Connections {
        target: biometric
        function onEnableFinished(success, errorMessage) {
            if (!success) biometricErrorLabel.text = errorMessage
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
            Label { text: qsTr("Fondo:"); color: theme.foreground; Layout.fillWidth: true }
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
            Label { text: qsTr("Texto general:"); color: theme.foreground; Layout.fillWidth: true }
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
            Label { text: qsTr("Bordes / botones:"); color: theme.foreground; Layout.fillWidth: true }
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
            Label { text: qsTr("Tu nombre en el chat:"); color: theme.foreground; Layout.fillWidth: true }
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
            Label { text: qsTr("Nombre del interlocutor:"); color: theme.foreground; Layout.fillWidth: true }
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
            Label { text: qsTr("Mensajes de sistema:"); color: theme.foreground; Layout.fillWidth: true }
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
            text: qsTr("Restaurar valores por defecto")
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

        Label {
            text: qsTr("Idioma")
            color: theme.accent
            font.bold: true
            Layout.topMargin: 8
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Idioma de la interfaz:"); color: theme.foreground; Layout.fillWidth: true }
            ComboBox {
                id: languageCombo
                textRole: "name"
                valueRole: "code"
                model: [
                    { code: "es", name: language.displayNameForCode("es") },
                    { code: "en", name: language.displayNameForCode("en") }
                ]
                Component.onCompleted: currentIndex = indexOfValue(language.currentCode)
            }
        }

        Label {
            text: qsTr("El cambio de idioma se aplica al reiniciar la app.")
            color: "#888888"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Version")
            color: theme.accent
            font.bold: true
            Layout.topMargin: 8
        }

        // Enlace fijo, siempre visible (haya o no version nueva) --
        // updateChecker.apkDownloadUrl baja el .apk directo (sin pasar por
        // la pagina del release, que tambien trae el zip/tar.gz del codigo
        // fuente que genera GitHub solo -- facil de bajar por error).
        Label {
            text: updateChecker.updateAvailable
                ? qsTr("Version %1 instalada -- hay una nueva: %2. <a href='%3'>Descargar</a>")
                      .arg(updateChecker.currentVersion).arg(updateChecker.latestVersion)
                      .arg(updateChecker.apkDownloadUrl)
                : qsTr("Version %1 instalada. <a href='%2'>Ver ultima version en GitHub</a>")
                      .arg(updateChecker.currentVersion).arg(updateChecker.apkDownloadUrl)
            textFormat: Text.RichText
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            wrapMode: Text.WordWrap
            color: theme.foreground
            Layout.fillWidth: true
        }

        // Oculto por completo si no hay hardware/huellas registradas (o en
        // escritorio, donde biometric.available siempre es false -- ver
        // BiometricBridge.cpp).
        RowLayout {
            visible: biometric.available
            Layout.fillWidth: true
            Layout.topMargin: 8

            Label {
                text: qsTr("Inicio de sesión con huella")
                color: theme.foreground
                Layout.fillWidth: true
            }

            Switch {
                id: biometricSwitch
                checked: biometric.enabled
                onClicked: {
                    // El clic ya cambio "checked" localmente y rompio el
                    // binding -- se restaura de inmediato para que el
                    // interruptor siempre refleje biometric.enabled de
                    // verdad, y la accion de activar/desactivar depende
                    // del estado anterior, no del visual a medio cambiar.
                    var wasEnabled = biometric.enabled
                    checked = Qt.binding(function() {
                        return biometric.enabled
                    })
                    if (wasEnabled) biometric.disable()
                    else biometricPasswordDialog.open()
                }
            }
        }

        Label {
            id: biometricErrorLabel
            visible: text.length > 0
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: "#ff6b6b"
        }
    }
}
