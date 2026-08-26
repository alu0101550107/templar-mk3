import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pagina raiz del StackView (ver Main.qml). "controller" es el
// ClientController expuesto como contexto global desde main.cpp -- toda la
// logica de red/cripto/persistencia vive ahi (ver
// phone/src/ClientController.cpp), esta pagina solo lee sus propiedades y
// llama a sus metodos, igual que MainWindow::onConnectClicked/
// onRegisterClicked/onLoginClicked en el cliente de escritorio pero
// separando vista y logica en vez de mezclarlas en la misma clase.
//
// Mismo aspecto que el cliente de escritorio: colores/fuente de
// Theme::defaults() (client/src/Theme.cpp) y el mismo fondo desenfocado que
// loginBackground_ (ver TemplarBackground.qml).
Page {
    id: page
    title: "Templar"
    background: null  // TemplarBackground de abajo ya cubre toda la pagina

    // controller.errorText es de solo lectura (ver ClientController.hpp) --
    // los errores de la huella (que no pasan por ClientController en
    // absoluto) necesitan su propio sitio donde guardarse.
    property string biometricError: ""

    // Navega en cuanto un login termina bien -- separado de "loggedIn" (que
    // tambien se pondria a true al restaurar sesion en un futuro arranque
    // automatico) para no disparar una navegacion no deseada en ese caso.
    Connections {
        target: controller
        function onLoginSucceeded() {
            page.StackView.view.push("ChatListPage.qml")
        }
    }

    // La huella nunca guarda el usuario (nunca fue secreto, ya vive en
    // rememberedUsername()) -- solo la contraseña, cifrada con la clave del
    // Keystore. Al confirmar la huella, se llama a controller.login() con
    // ambos exactamente igual que si se hubieran escrito a mano.
    Connections {
        target: biometric
        function onUnlockFinished(success, password, errorMessage) {
            if (success) {
                page.biometricError = ""
                controller.login(controller.rememberedUsername(), password)
            } else if (errorMessage !== "CANCELLED") {
                page.biometricError = errorMessage
            }
        }
    }

    // Precarga servidor/usuario del ultimo login -- la contrasena nunca se
    // recuerda (ver el comentario de rememberedServerAddress en
    // ClientController.hpp), asi que solo ahorra tener que retocar esos dos
    // campos cada vez, no un login de un solo toque.
    Component.onCompleted: {
        var savedHost = controller.rememberedServerAddress()
        if (savedHost.length > 0) hostField.text = savedHost
        var savedUser = controller.rememberedUsername()
        if (savedUser.length > 0) userField.text = savedUser
    }

    SettingsDialog {
        id: settingsDialog
    }

    TemplarBackground {
        anchors.fill: parent
        logoOpacity: 0.28
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            text: "TEMPLAR"
            font.family: "JetBrains Mono"
            font.pixelSize: 34
            font.bold: true
            font.letterSpacing: 6
            color: theme.accent
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 12
            Layout.bottomMargin: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TemplarTextField {
                id: hostField
                placeholderText: qsTr("Servidor (host:puerto)")
                text: "127.0.0.1:8080"
                enabled: !controller.connected
                Layout.fillWidth: true
            }

            TemplarButton {
                text: qsTr("Conectar")
                enabled: !controller.connected
                onClicked: controller.connectToServer(hostField.text)
            }
        }

        TemplarTextField {
            id: userField
            placeholderText: qsTr("Usuario")
            Layout.fillWidth: true
        }

        TemplarTextField {
            id: passField
            placeholderText: qsTr("Contraseña")
            echoMode: TextInput.Password
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TemplarButton {
                text: qsTr("Registrarse")
                enabled: controller.connected
                Layout.fillWidth: true
                onClicked: controller.registerAccount(userField.text, passField.text)
            }
            TemplarButton {
                text: qsTr("Iniciar sesion")
                enabled: controller.connected
                Layout.fillWidth: true
                onClicked: controller.login(userField.text, passField.text)
            }
        }

        // Oculto si no hay huella activada -- requiere estar conectado
        // igual que "Iniciar sesion", mismo motivo.
        TemplarButton {
            text: qsTr("Usar huella")
            visible: biometric.available && biometric.enabled
            enabled: controller.connected
            Layout.fillWidth: true
            onClicked: biometric.unlock()
        }

        // Mismo criterio que loginErrorLabel_ en el cliente de escritorio:
        // oculta mientras no haya error, texto en rojo cuando lo hay.
        Label {
            text: controller.errorText.length > 0 ? controller.errorText : page.biometricError
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: "#ff6b6b"
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        // Aviso discreto de version nueva -- oculto salvo que
        // updateChecker (comprobado una vez al arrancar, en segundo plano)
        // encuentre un release mas nuevo que este binario. Nunca bloquea
        // nada ni interrumpe con un dialogo, solo un enlace.
        Label {
            visible: updateChecker.updateAvailable
            text: qsTr("Hay una version nueva disponible (%1). <a href='%2'>Descargar</a>")
                .arg(updateChecker.latestVersion).arg(updateChecker.apkDownloadUrl)
            textFormat: Text.RichText
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            wrapMode: Text.WordWrap
            color: theme.accent
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        // Fila inferior: mismo patron que bottomRow en el cliente de
        // escritorio (MainWindow::MainWindow) -- el estado de conexion a la
        // izquierda (ocupa todo el ancho sobrante) y el boton de ajustes
        // pegado a la derecha, ambos en la misma fila.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: controller.statusText
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: theme.foreground
                Layout.fillWidth: true
            }

            TemplarButton {
                text: "⚙"  // engranaje -- icono, sin texto "Ajustes"
                font.pixelSize: 16
                implicitWidth: 36
                implicitHeight: 36
                onClicked: settingsDialog.open()
            }
        }
    }
}
