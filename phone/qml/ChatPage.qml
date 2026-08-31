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

    // --- Busqueda dentro de esta conversacion -- equivalente movil de
    // searchToggleButton_/searchBarWidget_ en el escritorio (ver
    // MainWindow::onSearchTextChanged/onSearchNextClicked/onSearchPrevClicked).
    // Adaptada a que aqui el historial es una lista de mensajes discretos
    // (ListView + ChatHistoryModel), no un unico texto continuo como el
    // QTextEdit del escritorio: cada "coincidencia" es un MENSAJE entero
    // que contiene la busqueda (ChatHistoryModel::findMatches), no una
    // posicion de caracter suelta -- navegar entre coincidencias
    // desplaza/centra la lista sobre ese mensaje en vez de mover un cursor
    // de texto.
    property bool searchBarVisible: false
    property var searchMatches: []
    property int searchMatchPos: -1

    function toggleSearchBar() {
        if (page.searchBarVisible) {
            closeSearchBar()
        } else {
            page.searchBarVisible = true
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }

    function closeSearchBar() {
        page.searchBarVisible = false
        searchField.clear()
        page.searchMatches = []
        page.searchMatchPos = -1
    }

    // Cada tecleo relanza la busqueda desde el principio -- mismo criterio
    // que onSearchTextChanged en el escritorio (siempre salta a la primera
    // coincidencia del texto que se acaba de escribir).
    function runSearch(query) {
        page.searchMatches = query.length > 0 ? controller.history.findMatches(query) : []
        page.searchMatchPos = page.searchMatches.length > 0 ? 0 : -1
        if (page.searchMatchPos >= 0) {
            historyView.positionViewAtIndex(page.searchMatches[page.searchMatchPos], ListView.Center)
        }
    }

    function searchNext() {
        if (page.searchMatches.length === 0) return
        page.searchMatchPos = (page.searchMatchPos + 1) % page.searchMatches.length
        historyView.positionViewAtIndex(page.searchMatches[page.searchMatchPos], ListView.Center)
    }

    function searchPrev() {
        if (page.searchMatches.length === 0) return
        page.searchMatchPos = (page.searchMatchPos - 1 + page.searchMatches.length) % page.searchMatches.length
        historyView.positionViewAtIndex(page.searchMatches[page.searchMatchPos], ListView.Center)
    }

    function refreshAdminStatus() {
        isGroupAdmin = page.isGroup && controller.groupAdmin(page.peerKey) === controller.username
    }

    function refreshOnlineStatus() {
        peerOnline = controller.conversations.isOnline(page.peerKey)
    }

    // Fragmento a mandar a controller.startReply al deslizar un mensaje --
    // mismo criterio y mismo limite que truncatedForQuote en MainWindow.cpp
    // del escritorio, para no arrastrar mensajes enteros (posiblemente muy
    // largos) dentro de cada respuesta.
    function truncatedForQuote(text) {
        var oneLine = text.replace(/\n/g, " ")
        return oneLine.length <= 80 ? oneLine : oneLine.substring(0, 80) + "…"
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

    // Fuente de iconos propia, empaquetada en el binario (ver
    // client/resources/icons/NOTICE.md) -- garantiza que simbolos como el
    // de enviar se vean igual en cualquier movil, sin depender de que el
    // sistema tenga esos codepoints en su fuente por defecto.
    FontLoader {
        id: iconFont
        source: "assets/templar-icons.ttf"
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
        title: qsTr("Selecciona un archivo para enviar")
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

            TemplarButton {
                text: "🔎"
                font.pixelSize: 16
                implicitWidth: 36
                flat: true
                onClicked: page.toggleSearchBar()
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
                text: qsTr("Salir")
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: page.searchBarVisible

            TemplarTextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar en esta conversacion...")
                onTextChanged: page.runSearch(text)
                onAccepted: page.searchNext()
            }
            Label {
                text: page.searchMatches.length > 0
                    ? (page.searchMatchPos + 1) + "/" + page.searchMatches.length
                    : (searchField.text.length > 0 ? qsTr("sin resultados") : "")
                color: theme.systemMessage
                font.pixelSize: 11
            }
            TemplarButton {
                text: "↑"
                implicitWidth: 32
                flat: true
                onClicked: page.searchPrev()
            }
            TemplarButton {
                text: "↓"
                implicitWidth: 32
                flat: true
                onClicked: page.searchNext()
            }
            TemplarButton {
                text: "✕"
                implicitWidth: 32
                flat: true
                onClicked: page.closeSearchBar()
            }
        }

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
                id: msgLabel
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

                // Vuelve a x:0 sola en cuanto se suelta el dedo (ver
                // DragHandler.onActiveChanged) -- solo mientras el gesto
                // esta activo se mueve sin animar, para que siga el dedo
                // sin retraso.
                Behavior on x {
                    enabled: !dragHandler.active
                    NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                }

                // Icono de responder, revelado a la izquierda del mensaje
                // segun se desliza -- hijo de msgLabel (no un hermano) para
                // que se mueva CON el, mismo efecto visual que WhatsApp sin
                // necesitar una capa de fondo aparte.
                Text {
                    text: "↩"
                    font.pixelSize: 18
                    color: theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                    x: -32
                    opacity: Math.max(0, Math.min(1, msgLabel.x / 48))
                }

                // Deslizar hacia la derecha para responder -- mismo gesto
                // que WhatsApp: siempre vuelve a x:0 al soltar (nunca se
                // queda "abierto"), y si se paso del umbral en el momento
                // de soltar, se activa startReply con este mensaje. Solo en
                // mensajes de texto propios/del interlocutor -- ni Sistema
                // (kind 0) ni un rawHtml (enlace de descarga) tiene sentido
                // citarlo, mismo criterio que el icono ↩ en formatLine del
                // escritorio.
                DragHandler {
                    id: dragHandler
                    target: msgLabel
                    enabled: model.kind !== 0 && !model.rawHtml
                    xAxis.enabled: true
                    yAxis.enabled: false
                    xAxis.minimum: 0
                    xAxis.maximum: 72
                    onActiveChanged: {
                        if (!dragHandler.active) {
                            if (msgLabel.x > 48) {
                                controller.startReply(model.who, page.truncatedForQuote(model.text))
                            }
                            msgLabel.x = 0
                        }
                    }
                }

                text: {
                    var prefix = ""
                    if (model.timestamp > 0) {
                        var hhmm = Qt.formatDateTime(new Date(model.timestamp * 1000), "HH:mm")
                        prefix = "<span style='color:" + theme.systemMessage + ";'>[" + hhmm + "] </span>"
                    }

                    // Escapado manual (< > &) -- RichText interpreta el
                    // texto como HTML, asi que sin esto un mensaje con "<"
                    // saldria mal (o, en el peor caso, con marcado roto).
                    // Se aplica tanto al cuerpo como a model.who (nombre de
                    // quien mando el mensaje): el servidor no valida el
                    // charset de un username (solo longitud), asi que sin
                    // escapar esto tambien, una cuenta con un nombre tipo
                    // "</b><a href=...>" podria inyectar marcado/enlaces en
                    // la vista de chat de cualquiera con quien hable --
                    // mismo criterio que ya aplica el cliente de escritorio
                    // (MainWindow.cpp, .toHtmlEscaped() sobre "who" siempre,
                    // independientemente de rawHtml).
                    function escapeHtml(s) {
                        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                    }

                    var who = escapeHtml(model.who)
                    // Saltos de linea reales como <br> -- solo tiene sentido
                    // sobre el cuerpo del mensaje, no sobre el nombre.
                    var body = model.rawHtml ? model.text : escapeHtml(model.text).replace(/\n/g, "<br>")

                    // Resalta la busqueda en el mensaje que la barra de
                    // busqueda tiene enfocado ahora mismo -- equivalente
                    // movil del resaltado de seleccion que QTextEdit::find()
                    // pinta solo -- en el escritorio (ver el comentario de
                    // searchMatches mas arriba: aqui la "coincidencia" es
                    // el mensaje entero, esto solo marca el texto dentro).
                    // Sobre `body` ya escapado (busca despues de escapar),
                    // asi que una busqueda que contenga literalmente "<",
                    // ">" o "&" puede no pintarse aunque el mensaje siga
                    // localizandose bien (eso lo resuelve ChatHistoryModel::
                    // findMatches sobre el texto plano).
                    if (!model.rawHtml && page.searchMatchPos >= 0 &&
                            page.searchMatches[page.searchMatchPos] === index &&
                            searchField.text.length > 0) {
                        var escapedQuery = searchField.text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
                        body = body.replace(new RegExp(escapedQuery, "gi"),
                            function(m) { return "<span style='background-color:" + theme.accent +
                                "; color:" + theme.background + ";'>" + m + "</span>" })
                    }

                    // Cita del mensaje al que se responde, si lo hay --
                    // DESPUES del resaltado de busqueda de arriba (para no
                    // marcar/afectar el contenido de la cita), bloque con
                    // borde izquierdo por encima del cuerpo, mismo patron
                    // visual que MainWindow::formatLine en el escritorio.
                    if (model.replyToText.length > 0) {
                        body = "<div style='border-left: 3px solid " + theme.accent +
                               "; padding-left: 6px; margin-bottom: 2px; color: " +
                               theme.systemMessage + ";'><b>" + escapeHtml(model.replyToSender) +
                               ":</b> " + escapeHtml(model.replyToText) + "</div>" + body
                    }

                    var leftCell, rightCell
                    if (model.kind === 0) {
                        leftCell = prefix
                        rightCell = "<i style='color:" + theme.systemMessage + ";'>[" + qsTr("SISTEMA") + "] " + body + "</i>"
                    } else {
                        var nameColor = model.kind === 1 ? theme.ownMessage : theme.peerMessage
                        leftCell = prefix + "<b style='color:" + nameColor + ";'>" + who + ":</b>"
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

        // Barra "Respondiendo a..." -- equivalente movil de replyBarWidget_
        // en el escritorio. Aparece al deslizar un mensaje (ver el
        // DragHandler del delegate de arriba) y se limpia sola al mandar o
        // al salir del chat (ver ClientController::setActiveConversation/
        // clearActiveConversation).
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: controller.hasPendingReply

            Label {
                text: qsTr("Respondiendo a %1: %2")
                    .arg(controller.pendingReplySender).arg(controller.pendingReplyText)
                wrapMode: Text.WordWrap
                color: theme.foreground
                Layout.fillWidth: true
            }
            TemplarButton {
                text: "✕"
                implicitWidth: 32
                flat: true
                onClicked: controller.cancelReply()
            }
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
                placeholderText: qsTr("Mensaje...")
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
                text: "➤"
                font.family: iconFont.name
                implicitWidth: 36
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Enviar")
                onClicked: page.sendCurrentMessage()
            }
        }
    }
}
