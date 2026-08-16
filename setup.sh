#!/usr/bin/env bash
# Templar mk3 -- instalacion para un cliente nuevo.
#
# Detecta la distro (Arch o Debian/Ubuntu), instala las dependencias que
# faltan, compila el proyecto, e instala el lanzador de escritorio (.desktop)
# y el comando `templar` para poder abrirlo desde cualquier terminal.
#
# Uso: ./setup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "==> Detectando distribucion..."
if command -v pacman >/dev/null 2>&1; then
  DISTRO="arch"
elif command -v apt-get >/dev/null 2>&1; then
  DISTRO="debian"
else
  echo "No se reconoce el gestor de paquetes (este script soporta Arch/pacman y Debian-Ubuntu/apt)." >&2
  echo "Instala manualmente: cmake, g++ (C++20), boost (headers), libsodium, sqlite3, openssl (headers + linea de comandos), Qt6 (base+widgets+network+herramientas de desarrollo, con soporte TLS), pkg-config." >&2
  exit 1
fi
echo "    Detectado: $DISTRO"

echo "==> Instalando dependencias (puede pedir tu contrasena de sudo)..."
if [ "$DISTRO" = "arch" ]; then
  sudo pacman -Sy --needed --noconfirm base-devel cmake boost libsodium sqlite openssl qt6-base pkgconf
elif [ "$DISTRO" = "debian" ]; then
  sudo apt-get update
  sudo apt-get install -y build-essential cmake libboost-dev libsodium-dev libsqlite3-dev \
    libssl-dev openssl qt6-base-dev qt6-base-dev-tools pkg-config
fi

echo "==> Configurando el proyecto con CMake..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando (puede tardar varios minutos la primera vez)..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

CLIENT_BIN="$BUILD_DIR/client/templar_client"
ICON_PATH="$SCRIPT_DIR/client/resources/great_helmet_logo.png"

if [ ! -x "$CLIENT_BIN" ]; then
  echo "La compilacion no genero el binario esperado en $CLIENT_BIN" >&2
  exit 1
fi

echo "==> Instalando el lanzador de escritorio (.desktop)..."
DESKTOP_DIR="$HOME/.local/share/applications"
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_DIR/templar-client.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Templar
Comment=Cliente de chat P2P cifrado extremo a extremo
Exec=$CLIENT_BIN
Icon=$ICON_PATH
Terminal=false
Categories=Network;Chat;InstantMessaging;
StartupWMClass=templar-client
EOF
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DESKTOP_DIR" || true

echo "==> Creando el comando 'templar' en ~/.local/bin..."
mkdir -p "$HOME/.local/bin"
ln -sf "$CLIENT_BIN" "$HOME/.local/bin/templar"

# Anade ~/.local/bin al PATH segun la shell del usuario, si no esta ya.
SHELL_NAME="$(basename "${SHELL:-}")"
case "$SHELL_NAME" in
  fish)
    if command -v fish >/dev/null 2>&1; then
      fish -c 'fish_add_path -U ~/.local/bin' || true
    fi
    ;;
  bash|zsh)
    RC="$HOME/.${SHELL_NAME}rc"
    if [ -f "$RC" ] && ! grep -qs '\.local/bin' "$RC"; then
      echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$RC"
      echo "    Anadido a $RC (abre una terminal nueva para que tenga efecto)"
    fi
    ;;
  *)
    echo "    No se reconoce tu shell automaticamente -- anade ~/.local/bin a tu PATH a mano si hace falta."
    ;;
esac

echo ""
echo "Listo. Puedes abrir Templar desde el menu de aplicaciones, o ejecutando:"
echo "    templar"
echo "(si el comando no se encuentra todavia, abre una terminal nueva)"
echo ""
echo "El servidor tambien se compilo, en: $BUILD_DIR/server/templar_server"
echo "(pero normalmente solo hace falta en la maquina que haga de servidor)"
echo ""
echo "Si lo que quieres es desplegar SOLO el servidor en otra maquina (sin"
echo "entorno grafico, sin instalar Qt6), usa deploy_server.sh en vez de este"
echo "script -- sube la carpeta del proyecto a esa maquina y ejecutalo ahi."
echo ""
echo "Nota sobre TLS: la primera vez que arranques el servidor genera solo un"
echo "certificado autofirmado (necesita 'openssl' en el PATH) e imprime su huella"
echo "por consola. El cliente confia en el certificado de cada servidor la primera"
echo "vez que se conecta a el y lo recuerda -- si esa huella cambiase mas adelante"
echo "sin que hayas reinstalado el servidor, el cliente cortara la conexion y avisara."
