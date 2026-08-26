import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Segunda pagina del stack. Lista de conversaciones real: controller.conversations
// es un ConversationListModel (ver phone/src/ConversationListModel.hpp),
// equivalente movil de conversations_/listedKeys_ en
// client/src/MainWindow.cpp.
Page {
    id: page
    title: qsTr("Conversaciones")
    // Sin logo desenfocado aqui a proposito -- se reserva para login y para
    // un chat abierto (mismo criterio que loginBackground_/chatBackground_
    // en escritorio: la lista en si no lo llevaba tampoco alli, era
    // conversationList_ sobre fondo solido dentro de chatBackground_).
    background: Rectangle { color: theme.background }

    SettingsDialog {
        id: settingsDialog
    }
    NewChatDialog {
        id: newChatDialog
    }
    CreateGroupDialog {
        id: createGroupDialog
    }

    // "Desconectar" solo pide la desconexion real (controller.connected
    // pasara a false cuando el socket se cierre de verdad); es esta
    // reaccion la que navega de vuelta al login, no el boton directamente
    // -- mismo patron que loginSucceeded en LoginPage.qml, y el mismo
    // orden de causalidad que el cliente de escritorio (onNetDisconnected
    // es quien cambia de pantalla, no onDisconnectClicked). Vive aqui y no
    // en Main.qml porque de momento es la unica pagina con boton de
    // desconectar; si ChatPage necesitara lo mismo mas adelante, se sube.
    Connections {
        target: controller
        function onConnectedChanged() {
            if (!controller.connected) {
                page.StackView.view.pop(null)
            }
        }
    }

    header: TemplarToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4

            Label {
                text: qsTr("Conectado como: %1").arg(controller.username)
                font.family: "JetBrains Mono"
                color: theme.accent
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            TemplarButton {
                // "⏻" (simbolo de apagado, bloque "Miscellaneous
                // Technical") no esta en la fuente de sistema de MIUI y se
                // veia como una caja rota. Un emoji de verdad si renderiza,
                // pero desentona con el resto (monocromo, color de acento)
                // al ser a todo color -- "×" es un simbolo normal de texto,
                // del mismo bloque ampliamente soportado que "×" en
                // Latin-1, y mantiene el mismo estilo que ←/⚙/+/-.
                text: "×"
                font.pixelSize: 18
                implicitWidth: 36
                onClicked: controller.disconnectFromServer()
            }
            TemplarButton {
                text: "⚙"  // engranaje -- icono, sin texto "Ajustes"
                font.pixelSize: 16
                implicitWidth: 36
                onClicked: settingsDialog.open()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Mismo criterio que loginErrorLabel_ en el cliente de escritorio:
        // oculto mientras no haya error (p.ej. un GroupErr al crear/invitar),
        // texto en rojo cuando lo hay.
        Label {
            text: controller.errorText
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: "#ff6b6b"
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.margins: 8
        }

        // Mismo aviso que en LoginPage.qml -- se repite aqui por si el
        // usuario ya habia pasado de esa pantalla cuando la comprobacion en
        // segundo plano termina.
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
            Layout.margins: 8
        }

        // Invitaciones a grupo pendientes -- equivalente movil del panel
        // "Invitaciones pendientes" de la barra lateral en escritorio
        // (inviteList_/acceptInviteButton_/rejectInviteButton_ en
        // MainWindow), pero con Aceptar/Rechazar en cada fila en vez de
        // seleccionar una fila y usar un par de botones compartido -- mas
        // natural al tacto.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4
            visible: controller.pendingInvites.length > 0

            Label {
                text: qsTr("Invitaciones pendientes")
                font.family: "JetBrains Mono"
                font.bold: true
                color: theme.accent
                Layout.fillWidth: true
            }

            Repeater {
                model: controller.pendingInvites

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: qsTr("%1 (invitado por %2)").arg(modelData.groupName).arg(modelData.inviter)
                        color: theme.foreground
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    TemplarButton {
                        text: qsTr("Aceptar")
                        font.pixelSize: 11
                        onClicked: controller.acceptGroupInvite(modelData.inviteId)
                    }
                    TemplarButton {
                        text: qsTr("Rechazar")
                        font.pixelSize: 11
                        onClicked: controller.rejectGroupInvite(modelData.inviteId)
                    }
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: controller.conversations
            delegate: TemplarItemDelegate {
                id: delegateRoot
                width: ListView.view.width
                onClicked: page.StackView.view.push("ChatPage.qml", { peerName: name, peerKey: key, isGroup: isGroup })

                // El nombre va centrado en el ANCHO TOTAL de la fila, no
                // solo en el hueco que deja el punto de presencia -- por
                // eso hay un "hueco" vacio identico a la derecha, del mismo
                // ancho que el de la izquierda (este SIEMPRE reserva su
                // espacio, tenga o no punto dentro, para que un grupo y un
                // chat 1-a-1 centren el texto exactamente en el mismo
                // sitio). RowLayout en vez de Item+anchors.fill: un Item
                // simple no tiene tamano implicito propio y Control no se
                // lo asigna solo por ser contentItem, asi que el Label
                // colapsaba a 0x0 (el punto se veia porque tiene ancho/alto
                // fijos propios). Con RowLayout el tamano lo gestiona el
                // layout de verdad -- y al darle Layout.preferredWidth a
                // los huecos, tambien lo hereda el Item que envuelve al
                // punto.
                contentItem: RowLayout {
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 30
                        Layout.fillHeight: true

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            visible: !isGroup
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            color: online ? "#2ecc71" : "#777777"
                        }
                    }

                    Label {
                        // Mismo formato "Nombre (N)" + negrita que
                        // updateSidebarUnreadStyle en el escritorio.
                        text: unread > 0 ? name + " (" + unread + ")" : name
                        font.bold: unread > 0
                        color: delegateRoot.pressed ? theme.background : theme.foreground
                        font.family: "JetBrains Mono"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Item {
                        Layout.preferredWidth: 30
                    }
                }
            }
        }
    }

    // Barra inferior con dos filas: los tres accesos principales arriba,
    // centrados, y debajo el mismo tipo de linea de estado que
    // LoginPage.qml -- Rectangle explicito porque el "footer" de Page trae
    // su propio panel de fondo claro por defecto en el estilo Basic, que
    // nuestros controles (fondo transparente en reposo) no tapan por si solos.
    footer: Rectangle {
        color: theme.background
        implicitHeight: footerColumn.implicitHeight + 16

        ColumnLayout {
            id: footerColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12

                TemplarButton {
                    text: qsTr("Nuevo chat")
                    onClicked: newChatDialog.open()
                }
                TemplarButton {
                    text: qsTr("Nuevo grupo")
                    onClicked: createGroupDialog.open()
                }
            }

            Label {
                text: qsTr("Conectado a %1").arg(controller.serverAddress)
                visible: controller.connected
                font.pixelSize: 11
                color: theme.foreground
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }
}
