import QtQuick

// Mismo tratamiento que BackgroundWidget en el cliente de escritorio (ver
// client/src/BackgroundWidget.cpp): color de fondo solido + el logo del
// casco desenfocado encima a baja opacidad, cubriendo todo el contenedor.
// El desenfoque se genera de antemano (great_helmet_logo_blurred.png) en
// vez de aplicarse en tiempo real via shader -- mismo resultado visual,
// sin depender de que MultiEffect renderice bien en cada dispositivo.
Item {
    id: root
    // 0.28 en el login, 0.12 en las pantallas de chat -- mismos valores que
    // loginBackground_/chatBackground_ en client/src/MainWindow.cpp.
    property real logoOpacity: 0.28

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    Image {
        anchors.fill: parent
        source: "assets/great_helmet_logo_blurred.png"
        fillMode: Image.PreserveAspectCrop
        opacity: root.logoOpacity
    }
}
