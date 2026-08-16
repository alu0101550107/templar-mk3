import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Selector de color propio -- deliberadamente NO usa ColorDialog
// (QtQuick.Dialogs): ese componente no expone background/contentItem para
// re-estilarlo, y la unica forma de oscurecerlo era forzar un estilo Qt
// Quick Controls global (Material), que de paso rompia el tamano/aspecto ya
// afinado de TemplarButton/TemplarTextField en el resto de la app. Con un
// control propio, sencillo (RGB con deslizadores), controlamos el aspecto
// entero nosotros mismos y no dependemos de ningun estilo externo en
// ningun sitio -- igual que el resto de componentes Templar*.
Dialog {
    id: picker
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 300, 300)

    // Color de entrada/salida: se fija antes de abrir (pickColor() en
    // SettingsDialog.qml) y se lee despues de aceptar.
    property color pickedColor: "white"

    // Copia de trabajo que mueven los deslizadores -- pickedColor no
    // cambia hasta aceptar, igual que el borrador de SettingsDialog.qml un
    // nivel mas arriba.
    property real r: 1
    property real g: 1
    property real b: 1

    onOpened: {
        r = pickedColor.r
        g = pickedColor.g
        b = pickedColor.b
    }

    onAccepted: pickedColor = Qt.rgba(r, g, b, 1.0)

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    header: Label {
        text: "Elegir color"
        color: theme.accent
        font.bold: true
        font.family: "JetBrains Mono"
        padding: 12
    }

    footer: RowLayout {
        width: picker.width
        spacing: 8

        TemplarButton {
            text: "Cancelar"
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: picker.reject()
        }
        TemplarButton {
            text: "Aceptar"
            Layout.fillWidth: true
            Layout.margins: 8
            onClicked: picker.accept()
        }
    }

    ColumnLayout {
        width: picker.availableWidth
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            radius: 4
            color: Qt.rgba(picker.r, picker.g, picker.b, 1.0)
            border.color: theme.accent
            border.width: 1
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "R"; color: theme.foreground; font.family: "JetBrains Mono" }
            TemplarSlider {
                from: 0; to: 1
                value: picker.r
                Layout.fillWidth: true
                onMoved: picker.r = value
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "G"; color: theme.foreground; font.family: "JetBrains Mono" }
            TemplarSlider {
                from: 0; to: 1
                value: picker.g
                Layout.fillWidth: true
                onMoved: picker.g = value
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "B"; color: theme.foreground; font.family: "JetBrains Mono" }
            TemplarSlider {
                from: 0; to: 1
                value: picker.b
                Layout.fillWidth: true
                onMoved: picker.b = value
            }
        }
    }
}
