import QtQuick
import QtQuick.Controls

// Mismo aspecto que QLineEdit en el cliente de escritorio (ver
// "QLineEdit, QTextEdit, QListWidget" en client/src/Theme.cpp):
// fondo casi negro semitransparente, borde de acento, texto claro. El
// fondo semitransparente es rgba(0,0,0,140) FIJO en el propio QSS del
// escritorio (no depende de theme.background alli tampoco), asi que se deja
// igual de fijo aqui a proposito -- toda la demas leida de "theme".
TextField {
    id: control

    color: theme.foreground
    placeholderTextColor: "#888888"
    selectionColor: theme.accent
    selectedTextColor: theme.background
    font.family: "JetBrains Mono"

    background: Rectangle {
        // #8C000000 = rgba(0, 0, 0, 140) -- el mismo tono fijo que usa Theme.cpp
        color: "#8c000000"
        border.color: theme.accent
        border.width: 1
        radius: 2
    }
}
