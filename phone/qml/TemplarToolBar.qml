import QtQuick
import QtQuick.Controls

// Cabecera oscura con una linea inferior de acento, en vez del gris claro
// por defecto -- mismo lenguaje visual que el resto de controles (ver
// TemplarButton.qml/TemplarTextField.qml), colores leidos de "theme".
ToolBar {
    id: control
    font.family: "JetBrains Mono"

    background: Rectangle {
        color: theme.background

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: theme.accent
        }
    }
}
