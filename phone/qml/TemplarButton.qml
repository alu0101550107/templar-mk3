import QtQuick
import QtQuick.Controls

// Mismo aspecto que QPushButton en el cliente de escritorio (ver
// "QPushButton" / ":hover" / ":pressed" en client/src/Theme.cpp): fondo
// transparente con borde y texto de color de acento en reposo, se rellena
// de acento con texto de fondo al pasar el raton, y de "foreground" con
// texto de fondo al pulsar. Colores leidos de "theme" (ThemeController,
// expuesto globalmente desde main.cpp) en vez de fijos, para que Ajustes
// pueda cambiarlos en caliente en toda la app.
Button {
    id: control

    hoverEnabled: true
    font.family: "JetBrains Mono"

    // Sin esto, tocar CUALQUIER boton le roba el foco al campo de texto
    // activo -- y en Android perder el foco de un campo de texto cierra el
    // teclado en pantalla al momento. Los botones de esta app viven casi
    // siempre al lado de un TextField (enviar, emoji, adjuntar...), asi que
    // nunca deberian quedarse con el foco de teclado.
    focusPolicy: Qt.NoFocus

    contentItem: Text {
        text: control.text
        font: control.font
        color: (control.down || control.hovered) ? theme.background : theme.accent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: control.down ? theme.foreground : (control.hovered ? theme.accent : "transparent")
        border.color: theme.accent
        border.width: 1
        radius: 2
    }
}
