#include "EmojiPicker.hpp"

#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace templar::client {

namespace {

// Set curado de emojis comunes, no el Unicode completo (serian miles y
// pedirian busqueda/paginacion para ser usables) -- suficiente para el uso
// normal de un chat entre amigos. Cada string es una lista separada por
// espacios.
struct Category {
  const char* title;
  const char* emojis;
};

constexpr Category kCategories[] = {
    {"Caras",
     "😀 😃 😄 😁 😆 😅 🤣 😂 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤑 🤗 🤭 🤫 🤔 😐 😑 😶 🙄 😏 "
     "😴 🤤 😪 😮 😲 🥺 😢 😭 😤 😡 🤯 🥳 😎 🤓 🥵 🥶 😷 🤒"},
    {"Gestos", "👍 👎 👌 ✌️ 🤞 🤟 🤘 👋 🤙 💪 🙏 👏 🙌 🤝 👊 ✊ 👆 👇 👉 👈 🖐️ ✋ 🤚 💅 🤦 🤷"},
    {"Amor", "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 💕 💞 💓 💗 💖 💘 💝 💟"},
    {"Animales",
     "🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🐔 🐧 🐦 🦆 🦉 🦄 🐴 🐝 🐛 🦋 🐌 🐢 🐍 🐙 🐬 🐳 "
     "🐘 🦒 🌵 🌲 🌸 🌻 🌈 ☀️ 🌙 ⭐ ⚡ 🔥 💧 ❄️"},
    {"Comida",
     "🍏 🍎 🍌 🍉 🍇 🍓 🍒 🍑 🥭 🍍 🥑 🍆 🍕 🍔 🍟 🌭 🥪 🌮 🌯 🍣 🍜 🍝 🍿 🍩 🍪 🎂 🍰 🍫 🍬 🍭 🍺 "
     "🍻 🍷 🍹 ☕ 🍵"},
    {"Actividades", "⚽ 🏀 🏈 ⚾ 🎾 🏐 🏓 🎮 🎲 🎯 🎳 🎸 🎧 🎨 🎬 🏆 🥇 🎉 🎊 🎁 🎈"},
    {"Objetos", "💡 📱 💻 ⌨️ 🖥️ 🖨️ 📷 📸 🔋 🔌 💰 💵 💎 🔧 🔨 🔒 🔑 📌 📎 ✂️ 📝 📅 ⏰ ⌚"},
    {"Simbolos", "✅ ❌ ❓ ❗ ⚠️ 🚫 ♻️ 🔴 🟠 🟡 🟢 🔵 🟣 ⚪ ⚫ ➕ ➖ ➗ ✖️ 💯 🆗 🆕"},
};

constexpr int kColumns = 8;
constexpr int kButtonSize = 40;

// El tema visual global (Theme::toQss) le pone a TODO QPushButton de la app
// un padding de 5px 12px y un borde de 1px, pensado para botones con texto
// ("Enviar", "Desconectar"...). Con un boton fijado a 32x32 ese padding se
// comia casi todo el espacio y el glifo del emoji salia recortado. Al fijar
// el stylesheet directamente en cada boton (mas especifico que el de la
// app) se anula ese padding solo aqui, sin tocar el resto de botones.
constexpr const char* kButtonStyle =
    "QPushButton { padding: 0px; border: none; border-radius: 4px; }"
    "QPushButton:hover:!disabled { background-color: rgba(255, 255, 255, 40); }";

}  // namespace

EmojiPicker::EmojiPicker(QWidget* parent) : QWidget(parent, Qt::Popup) {
  setObjectName("emojiPicker");
  setAttribute(Qt::WA_DeleteOnClose);

  auto* tabs = new QTabWidget();
  tabs->setObjectName("emojiTabs");
  for (const Category& cat : kCategories) {
    QStringList emojis =
        QString::fromUtf8(cat.emojis).split(' ', Qt::SkipEmptyParts);
    buildCategory(tabs, QString::fromUtf8(cat.title), emojis);
  }

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->addWidget(tabs);
  resize(360, 280);
}

void EmojiPicker::buildCategory(QTabWidget* tabs, const QString& title, const QStringList& emojis) {
  auto* page = new QWidget();
  auto* grid = new QGridLayout(page);
  grid->setSpacing(2);

  for (int i = 0; i < emojis.size(); ++i) {
    QString emoji = emojis[i];
    auto* button = new QPushButton(emoji);
    // objectName con el propio emoji: permite a los tests automatizados
    // encontrar y pulsar uno concreto sin depender del orden en la rejilla.
    button->setObjectName("emoji_" + emoji);
    button->setFlat(true);
    button->setStyleSheet(kButtonStyle);
    button->setFixedSize(kButtonSize, kButtonSize);
    QFont font = button->font();
    font.setPointSize(font.pointSize() + 6);
    button->setFont(font);
    connect(button, &QPushButton::clicked, this, [this, emoji] { emit emojiSelected(emoji); });
    grid->addWidget(button, i / kColumns, i % kColumns);
  }

  auto* scroll = new QScrollArea();
  scroll->setWidget(page);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFrameShape(QFrame::NoFrame);
  tabs->addTab(scroll, title);
}

}  // namespace templar::client
