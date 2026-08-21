import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window

    // --- Tamano de ventana ---
    // En Android/iOS la ventana YA es la pantalla del movil -- pedir un
    // tamano tipo "movil medio" ahi (pensado para simular un movil DENTRO
    // de un monitor de escritorio) la encoge a una fraccion de si misma en
    // vez de ocupar la pantalla entera. isMobilePlatform separa ambos
    // casos. En movil NO se fija width/height a mano (ni Screen.width
    // tampoco -- eso es el alto FISICO total de la pantalla, e incluye el
    // area de las barras del sistema, asi que forzarlo corta el contenido
    // por abajo): se deja sin asignar para que la plataforma reserve ella
    // misma el area realmente disponible bajo la barra de estado y encima
    // de la de navegacion, igual que hace cualquier app Android normal. En
    // escritorio se mantiene la simulacion de tamano de movil de siempre.
    readonly property bool isMobilePlatform: Qt.platform.os === "android" || Qt.platform.os === "ios"

    readonly property real minWindowWidth: 320
    readonly property real maxWindowWidth: 480
    readonly property real phoneAspectRatio: 19.5 / 9

    width: isMobilePlatform ? undefined
        : Math.max(minWindowWidth, Math.min(maxWindowWidth, Screen.desktopAvailableWidth * 0.28))
    height: isMobilePlatform ? undefined
        : Math.min(Screen.desktopAvailableHeight * 0.85, width * phoneAspectRatio)

    // Sin esto, la ventana sigue siendo redimensionable aunque le pidamos
    // un tamano inicial concreto -- y la mayoria de gestores de ventanas en
    // mosaico (Hyprland, i3, sway...) tilean cualquier ventana redimensionable
    // como una mas, ignorando el tamano pedido. minimumSize == maximumSize
    // es la convencion estandar en Linux para decir "esta ventana es de
    // tamano fijo, tratala como flotante", y la mayoria de esos gestores la
    // respetan. En movil no aplica: la ventana no es flotante ni se puede
    // redimensionar de todos modos.
    minimumWidth: isMobilePlatform ? undefined : width
    maximumWidth: isMobilePlatform ? undefined : width
    minimumHeight: isMobilePlatform ? undefined : height
    maximumHeight: isMobilePlatform ? undefined : height

    // --- Escala del contenido ---
    // Ancho de referencia sobre el que esta pensado el diseno (un movil
    // "medio" tipico). Fuentes y margenes se derivan de esta escala en vez
    // de usar pixeles fijos, para verse proporcionados tanto en un movil
    // pequeno como en uno grande -- al ser un binding (no una asignacion
    // puntual), se recalcula solo si la ventana cambia de tamano.
    readonly property real referenceWidth: 400
    readonly property real scaleFactor: Math.max(0.85, Math.min(1.3, width / referenceWidth))

    visible: true
    title: "Templar"

    // Boton/gesto "atras" de Android: sin esto, Qt trata el evento como un
    // cierre de ventana normal y la app entera se sale en vez de navegar.
    // Los Dialog/Popup (SettingsDialog, NewChatDialog, ...) no necesitan
    // nada aparte -- QtQuick Controls ya los cierra solo con el boton atras
    // (closePolicy: Popup.CloseOnEscape se dispara tambien con Key_Back).
    // Solo interceptamos por debajo de ChatListPage (stackView.depth > 2,
    // es decir "estoy dentro de un chat"): ahi hacemos pop() a la lista en
    // vez de salir. En ChatListPage (depth 2) o LoginPage (depth 1) se deja
    // el comportamiento por defecto -- salir de la app, igual que cualquier
    // pantalla "raiz" en Android -- para no reaparecer en el login estando
    // aun conectados. Solo aplica en movil: en escritorio la X de la
    // ventana debe cerrar sin mas, pase lo que pase en el stack.
    onClosing: (close) => {
        if (isMobilePlatform && stackView.depth > 2) {
            close.accepted = false
            stackView.pop()
        }
    }

    // Contenedor de navegacion: cada "pantalla principal" (login, lista de
    // chats, chat en concreto) es una Page que se push()/pop() aqui encima,
    // en vez de una ventana nueva -- en movil solo hay UNA ventana. Las
    // "ventanas pequenas" (ajustes, nuevo chat/grupo) mas adelante seran
    // Dialog/Drawer flotando sobre la pagina activa, no pantallas del stack.
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: "LoginPage.qml"
    }
}
