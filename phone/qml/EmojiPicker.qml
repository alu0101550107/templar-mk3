import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Calco movil de EmojiPicker.cpp/.hpp del escritorio: mismas categorias y
// listas de emojis, pero como Popup de QML en vez de QWidget con
// Qt::Popup -- mismo comportamiento (se cierra solo al pulsar fuera o
// pulsar Escape), sin necesidad de una ventana nativa aparte. No incluye
// ningun asset de imagen propio: solo inserta el caracter unicode, la
// fuente de emoji del sistema es quien decide como se dibuja.
Popup {
    id: popup

    signal emojiSelected(string emoji)

    width: 320
    height: 260
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.background
        border.color: theme.accent
        border.width: 1
        radius: 4
    }

    // Mismo set curado y mismo orden que kCategories en EmojiPicker.cpp.
    readonly property var categories: [
        { title: "Caras", emojis: "😀 😃 😄 😁 😆 😅 🤣 😂 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤑 🤗 🤭 🤫 🤔 😐 😑 😶 🙄 😏 😴 🤤 😪 😮 😲 🥺 😢 😭 😤 😡 🤯 🥳 😎 🤓 🥵 🥶 😷 🤒".split(" ") },
        { title: "Gestos", emojis: "👍 👎 👌 ✌️ 🤞 🤟 🤘 👋 🤙 💪 🙏 👏 🙌 🤝 👊 ✊ 👆 👇 👉 👈 🖐️ ✋ 🤚 💅 🤦 🤷".split(" ") },
        { title: "Amor", emojis: "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 💕 💞 💓 💗 💖 💘 💝 💟".split(" ") },
        { title: "Animales", emojis: "🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🐔 🐧 🐦 🦆 🦉 🦄 🐴 🐝 🐛 🦋 🐌 🐢 🐍 🐙 🐬 🐳 🐘 🦒 🌵 🌲 🌸 🌻 🌈 ☀️ 🌙 ⭐ ⚡ 🔥 💧 ❄️".split(" ") },
        { title: "Comida", emojis: "🍏 🍎 🍌 🍉 🍇 🍓 🍒 🍑 🥭 🍍 🥑 🍆 🍕 🍔 🍟 🌭 🥪 🌮 🌯 🍣 🍜 🍝 🍿 🍩 🍪 🎂 🍰 🍫 🍬 🍭 🍺 🍻 🍷 🍹 ☕ 🍵".split(" ") },
        { title: "Actividades", emojis: "⚽ 🏀 🏈 ⚾ 🎾 🏐 🏓 🎮 🎲 🎯 🎳 🎸 🎧 🎨 🎬 🏆 🥇 🎉 🎊 🎁 🎈".split(" ") },
        { title: "Objetos", emojis: "💡 📱 💻 ⌨️ 🖥️ 🖨️ 📷 📸 🔋 🔌 💰 💵 💎 🔧 🔨 🔒 🔑 📌 📎 ✂️ 📝 📅 ⏰ ⌚".split(" ") },
        { title: "Simbolos", emojis: "✅ ❌ ❓ ❗ ⚠️ 🚫 ♻️ 🔴 🟠 🟡 🟢 🔵 🟣 ⚪ ⚫ ➕ ➖ ➗ ✖️ 💯 🆗 🆕".split(" ") }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }

            Repeater {
                model: popup.categories
                TabButton {
                    id: tabButton
                    text: modelData.title
                    font.family: "JetBrains Mono"
                    font.pixelSize: 10
                    contentItem: Text {
                        text: tabButton.text
                        font: tabButton.font
                        color: theme.foreground
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: theme.accent
                            visible: tabButton.checked
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            Repeater {
                model: popup.categories

                GridView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    cellWidth: 36
                    cellHeight: 36
                    clip: true
                    model: modelData.emojis

                    delegate: ItemDelegate {
                        width: 36
                        height: 36

                        contentItem: Text {
                            text: modelData
                            font.pixelSize: 20
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.hovered || parent.down ? theme.accent : "transparent"
                            opacity: 0.3
                            radius: 4
                        }
                        onClicked: {
                            popup.emojiSelected(modelData)
                            popup.close()
                        }
                    }
                }
            }
        }
    }
}
