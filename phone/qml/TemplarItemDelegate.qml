import QtQuick
import QtQuick.Controls

// Fila de lista oscura -- mismo criterio que "QListWidget::item:selected"
// en client/src/Theme.cpp: fondo de acento y texto de fondo mientras se
// esta pulsando, texto claro en reposo. El resaltado al pasar el raton
// (hover) y la linea separadora no existen como tales en el QSS de
// escritorio (QListWidget no define ::item:hover alli), asi que se dejan
// fijos aqui tambien en vez de inventarles un campo de Theme que no existe.
ItemDelegate {
    id: control
    font.family: "JetBrains Mono"

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.pressed ? theme.background : theme.foreground
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
    }

    background: Rectangle {
        color: control.pressed ? theme.accent : (control.hovered ? "#1a1a1a" : "transparent")

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#222222"
        }
    }
}
