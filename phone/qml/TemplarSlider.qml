import QtQuick
import QtQuick.Controls

// Slider propio, mismo motivo que el resto de componentes Templar*: el
// control por defecto no lee de "theme" pase lo que pase con el estilo
// activo (ver TemplarColorPicker.qml, que es quien lo usa).
Slider {
    id: control

    // Slider calcula su propio implicitHeight/implicitWidth a partir del
    // implicitHeight/implicitWidth de background/handle -- NO de su
    // height/width. Un Rectangle no rellena implicitHeight solo por
    // fijarle height, asi que sin esto el control entero media 0px de
    // alto: se veia (el Rectangle se pinta igual) pero no habia area
    // ninguna donde hacer clic o arrastrar.
    implicitHeight: 24
    implicitWidth: 200

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: "#333333"

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: theme.accent
        border.color: theme.foreground
        border.width: 1
    }
}
