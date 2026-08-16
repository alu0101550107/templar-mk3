import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Tercera pagina del stack: un chat en concreto. Se le pasan peerName (para
// el titulo), peerKey (el username 1-a-1 o id de grupo -- lo que
// ConversationListModel usa como clave) e isGroup al hacer
// push("ChatPage.qml", { ... }) desde ChatListPage.qml.
Page {
    id: page
    property string peerName: "?"
    property string peerKey: ""
    property bool isGroup: false
    // Solo el admin de un grupo ve los botones +/- (anadir/expulsar
    // miembro) -- mismo criterio que addMemberButton_/kickButton_ en
    // MainWindow::updateGroupHeader del escritorio. No es una property
    // binding normal porque controller.groupAdmin()/groupMembers() son
    // lecturas puntuales (Q_INVOKABLE), no propiedades NOTIFYables: hay que
    // refrescarla a mano cuando llega groupInfoChanged.
    property bool isGroupAdmin: false
    // Punto de presencia junto al nombre en la cabecera -- mismo dato
    // (ConversationListModel::online) que el punto de ChatListPage.qml,
    // pero leido a mano en vez de con un role de modelo porque aqui no hay
    // ListView/delegate de por medio.
    property bool peerOnline: false

    function refreshAdminStatus() {
        isGroupAdmin = page.isGroup && controller.groupAdmin(page.peerKey) === controller.username
    }

    function refreshOnlineStatus() {
        peerOnline = controller.conversations.isOnline(page.peerKey)
    }

    Component.onCompleted: {
        refreshAdminStatus()
        refreshOnlineStatus()
        controller.setActiveConversation(page.peerKey)
    }

    // Se dispara siempre que esta pagina se destruye, sea cual sea el
    // motivo (flecha de atras, gesto del sistema, o el boton "Salir" de
    // un grupo) -- a diferencia de ponerlo solo en el onClicked de la
    // flecha, esto cubre TODAS las formas de salir del chat.
    Component.onDestruction: controller.clearActiveConversation()

    Connections {
        target: controller
        function onGroupInfoChanged(key) {
            if (key === page.peerKey) page.refreshAdminStatus()
        }
    }

    // ConversationListModel no tiene una senal propia de "cambio de
    // presencia" -- reusa dataChanged (la senal estandar de
    // QAbstractItemModel) directamente, igual de valido que una senal a
    // medida ya que solo nos interesa "algo cambio, vuelve a leer".
    Connections {
        target: controller.conversations
        function onDataChanged() { page.refreshOnlineStatus() }
    }

    title: peerName
    background: null  // TemplarBackground de abajo ya cubre toda la pagina

    TemplarBackground {
        anchors.fill: parent
        logoOpacity: 0.12  // mismo valor que chatBackground_ en escritorio
    }

    EmojiPicker {
        id: emojiPicker
        // Se abre justo encima de la fila de envio, centrado -- se
        // comporta como un desplegable, igual que en el escritorio (ahi se
        // posiciona justo encima de emojiButton_).
        x: (page.width - width) / 2
        y: page.height - height - 68
        onEmojiSelected: function (emoji) { messageField.insert(messageField.cursorPosition, emoji) }
    }

    AddMemberDialog {
        id: addMemberDialog
        groupKey: page.peerKey
    }
    KickMemberDialog {
        id: kickMemberDialog
        groupKey: page.peerKey
    }

    FileDialog {
        id: fileDialog
        title: "Selecciona un archivo para enviar"
        onAccepted: controller.sendFile(page.peerKey, selectedFile)
    }

    header: TemplarToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4

            TemplarButton {
                // "<" a secas se ve como una caja rota en Android: el
                // control auto-detecta si el texto es HTML, y un "<" suelto
                // se interpreta como el inicio de una etiqueta invalida.
                // Una flecha de verdad no tiene ese problema.
                text: "←"
                font.pixelSize: 20
                implicitWidth: 36
                flat: true
                onClicked: page.StackView.view.pop()
            }

            // Nombre + punto de presencia centrados como una unidad: un
            // hueco a cada lado del mismo ancho (uno con el punto dentro,
            // el otro vacio) para que el Label de en medio quede centrado
            // en el mismo sitio tenga o no punto (grupo vs. chat 1-a-1) --
            // mismo truco que el ListView de ChatListPage.qml.
            Item {
                Layout.preferredWidth: 18
                visible: !page.isGroup

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    color: page.peerOnline ? "#2ecc71" : "#777777"
                }
            }

            Label {
                text: page.title
                font.family: "JetBrains Mono"
                font.bold: true
                color: theme.accent
                elide: Text.ElideRight
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item {
                Layout.preferredWidth: 18
                visible: !page.isGroup
            }

            // +/- solo visibles para el admin de un grupo -- si no son
            // visibles, este hueco vacio conserva el mismo ancho que el
            // boton de atras para que el titulo se mantenga centrado en el
            // caso normal (chat 1-a-1 o miembro no-admin).
            Item {
                Layout.preferredWidth: 36
                visible: !page.isGroupAdmin
            }
            TemplarButton {
                text: "+"
                font.pixelSize: 18
                implicitWidth: 36
                visible: page.isGroupAdmin
                onClicked: addMemberDialog.open()
            }
            TemplarButton {
                text: "-"
                font.pixelSize: 18
                implicitWidth: 36
                visible: page.isGroupAdmin
                onClicked: kickMemberDialog.open()
            }
            TemplarButton {
                // A diferencia de +/- (solo admin), salir del grupo lo
                // puede hacer cualquier miembro -- mismo criterio que
                // leaveGroupButton_ en el escritorio.
                text: "Salir"
                font.pixelSize: 11
                visible: page.isGroup
                onClicked: {
                    controller.leaveGroup(page.peerKey)
                    page.StackView.view.pop()
                }
            }
        }
    }

    // Envia el texto del campo de mensaje si no esta vacio y limpia el
    // campo -- comparten esta funcion el boton "Enviar" y pulsar Enter en
    // el propio campo.
    function sendCurrentMessage() {
        var text = messageField.text.trim()
        if (text.length === 0) return
        if (page.isGroup) {
            controller.sendGroupMessage(page.peerKey, text)
        } else {
            controller.sendMessage(page.peerKey, text)
        }
        messageField.clear()
        // Por si el foco se movio igualmente (p.ej. al pulsar Enviar) --
        // recuperarlo aqui mantiene el teclado abierto para el siguiente
        // mensaje en vez de tener que volver a tocar el campo cada vez.
        messageField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        ListView {
            id: historyView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: controller.history

            // Separador de fecha entre mensajes de dias distintos --
            // mismo criterio que MainWindow::dateDividerHtml/
            // appendLiveLine en el escritorio, pero resuelto con el
            // mecanismo de secciones nativo de ListView en vez de
            // insertar una linea mas a mano en el modelo (ver
            // ChatHistoryModel::DateSectionRole).
            section.property: "dateSection"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                width: ListView.view.width
                // Los mensajes sin hora (timestamp <= 0, de antes de que
                // el historial la guardara) comparten seccion "" -- sin
                // texto que mostrar, no se reserva alto para su cabecera.
                height: section.length > 0 ? dateLabel.implicitHeight + 8 : 0
                Label {
                    id: dateLabel
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    visible: section.length > 0
                    text: section
                    font.family: "JetBrains Mono"
                    font.italic: true
                    font.pixelSize: 11
                    color: theme.systemMessage
                }
            }

            // Autoscroll: cada linea nueva (propia o recibida) deja la
            // vista pegada al final, igual que un chat normal. Qt.callLater
            // en vez de llamarlo directo: el alto de un delegate con texto
            // envuelto (RichText multilinea) no esta recalculado todavia en
            // el momento exacto de este evento, asi que llamarlo sin
            // demora se queda corto. Tambien reacciona a heightChanged: al
            // abrirse el teclado la lista se encoge (sigue siendo
            // Layout.fillHeight dentro de la Column) sin que cambie el
            // numero de mensajes, y sin esto habia que bajar a mano cada
            // vez que se escribia.
            onCountChanged: Qt.callLater(positionViewAtEnd)
            onHeightChanged: Qt.callLater(positionViewAtEnd)

            // Mismo formato (y mismo truco de tabla HTML) que
            // MainWindow::formatLine en el escritorio: "[HH:MM] Usuario:"
            // en una celda que nunca envuelve, el mensaje en la celda de al
            // lado -- asi, si el mensaje ocupa varias lineas (por ser largo
            // y envolver, o por saltos de linea reales), el texto queda
            // alineado bajo si mismo en vez de irse al margen izquierdo
            // como si fuera un mensaje nuevo. Un unico Label con RichText
            // en vez de 3 Labels en RowLayout: con 3 columnas separadas el
            // mensaje quedaba a una altura distinta de la del nick en
            // cuanto envolvia, porque cada Label calculaba su propio alto
            // por su cuenta -- la tabla los resuelve juntos, como una unica
            // unidad.
            delegate: Label {
                width: ListView.view.width
                wrapMode: Text.Wrap
                textFormat: Text.RichText
                font.family: "JetBrains Mono"
                topPadding: 2
                bottomPadding: 2
                leftPadding: 4
                rightPadding: 4
                color: theme.foreground
                // Solo el enlace "Descargar" de un FileBlobPointer trae
                // rawHtml=true -- lo generamos nosotros mismos a proposito
                // (ver ClientController::onFileBlobPointerReceived), asi
                // que aqui se deja pasar tal cual en vez de escaparlo.
                onLinkActivated: function(link) {
                    if (link.indexOf("templar-download:") === 0) {
                        controller.startBlobDownload(link.substring("templar-download:".length))
                    } else if (link.indexOf("templar-selffile:") === 0) {
                        controller.openSelfFile(decodeURIComponent(link.substring("templar-selffile:".length)))
                    }
                }

                text: {
                    var prefix = ""
                    if (model.timestamp > 0) {
                        var hhmm = Qt.formatDateTime(new Date(model.timestamp * 1000), "HH:mm")
                        prefix = "<span style='color:" + theme.systemMessage + ";'>[" + hhmm + "] </span>"
                    }

                    // Escapado manual (< > &) y saltos de linea reales
                    // como <br> -- RichText interpreta el texto como HTML,
                    // asi que sin esto un mensaje con "<" o varias lineas
                    // saldria mal (o, en el peor caso, con marcado roto).
                    var body = model.rawHtml ? model.text : model.text
                        .replace(/&/g, "&amp;")
                        .replace(/</g, "&lt;")
                        .replace(/>/g, "&gt;")
                        .replace(/\n/g, "<br>")

                    var leftCell, rightCell
                    if (model.kind === 0) {
                        leftCell = prefix
                        rightCell = "<i style='color:" + theme.systemMessage + ";'>[SISTEMA] " + body + "</i>"
                    } else {
                        var nameColor = model.kind === 1 ? theme.ownMessage : theme.peerMessage
                        leftCell = prefix + "<b style='color:" + nameColor + ";'>" + model.who + ":</b>"
                        rightCell = body
                    }

                    return "<table style='margin:0;' cellspacing='0' cellpadding='0'><tr>" +
                           "<td style='white-space:nowrap; vertical-align:top; padding-right:6px;'>" +
                           leftCell + "</td><td style='vertical-align:top;'>" + rightCell +
                           "</td></tr></table>"
                }
            }
        }

        // Estado de la transferencia de archivo en curso (envio o
        // recepcion) -- equivalente movil de transferLabel_/
        // transferProgress_ en el escritorio.
        Label {
            text: controller.fileTransferStatus
            visible: text.length > 0
            color: theme.systemMessage
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ProgressBar {
            visible: controller.fileTransferStatus.length > 0
            from: 0
            to: 1
            value: controller.fileTransferProgress
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            // "Sistema" (peerKey vacio) es solo lectura -- mismo criterio
            // que isRealChat en MainWindow::onConversationSelected del
            // escritorio.
            visible: page.peerKey !== ""

            TemplarTextField {
                id: messageField
                placeholderText: "Mensaje..."
                Layout.fillWidth: true
                onAccepted: page.sendCurrentMessage()
            }
            TemplarButton {
                text: "😀"
                implicitWidth: 36
                onClicked: emojiPicker.open()
            }
            TemplarButton {
                text: "📎"
                implicitWidth: 36
                onClicked: fileDialog.open()
            }
            TemplarButton {
                text: "📷"
                implicitWidth: 36
                onClicked: controller.capturePhoto(page.peerKey)
            }
            TemplarButton {
                text: "Enviar"
                onClicked: page.sendCurrentMessage()
            }
        }
    }
}
