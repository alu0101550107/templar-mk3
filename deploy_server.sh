#!/usr/bin/env bash
# Templar mk3 -- despliegue de SOLO SERVIDOR en una maquina nueva (sin
# entorno grafico).
#
# Pensado para: subir esta carpeta entera (p.ej. con scp/rsync) a la
# maquina que va a hacer de servidor, y ejecutar este script ahi. Detecta
# la distro (Arch o Debian/Ubuntu), instala SOLO las dependencias que
# necesita el servidor (nada de Qt6 -- ver la opcion TEMPLAR_SERVER_ONLY en
# el CMakeLists.txt raiz), compila unicamente el binario templar_server, y
# lo deja listo para ejecutar. NO instala nada en systemd ni arranca
# ningun servicio -- eso se deja a proposito para que lo hagas tu, con el
# usuario/puerto/directorio que prefieras (al final se imprime un archivo
# .service de ejemplo ya generado con las rutas correctas para copiar).
#
# Uso: ./deploy_server.sh
# Se puede volver a ejecutar despues de subir una version nueva del
# codigo (p.ej. tras un git pull) para recompilar -- es incremental.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-server"

echo "==> Detectando distribucion..."
if command -v pacman >/dev/null 2>&1; then
  DISTRO="arch"
elif command -v apt-get >/dev/null 2>&1; then
  DISTRO="debian"
else
  echo "No se reconoce el gestor de paquetes (este script soporta Arch/pacman y Debian-Ubuntu/apt)." >&2
  echo "Instala manualmente: cmake, g++ (C++20), boost (headers), libsodium, sqlite3, openssl (headers + linea de comandos), pkg-config." >&2
  exit 1
fi
echo "    Detectado: $DISTRO"

echo "==> Instalando dependencias del servidor (puede pedir tu contrasena de sudo)..."
echo "    (a proposito NO incluye Qt6 -- el servidor no lo necesita para nada)"
if [ "$DISTRO" = "arch" ]; then
  sudo pacman -Sy --needed --noconfirm base-devel cmake boost libsodium sqlite openssl pkgconf curl
elif [ "$DISTRO" = "debian" ]; then
  sudo apt-get update
  sudo apt-get install -y build-essential cmake libboost-dev libsodium-dev libsqlite3-dev \
    libssl-dev openssl pkg-config curl
fi

# La libreria de cripto (crypto/src/X3DH.cpp, DoubleRatchet.cpp) usa HKDF
# (crypto_kdf_hkdf_sha256_*), que libsodium no tuvo hasta la version 1.0.19.
# Las distros con paquetes "estables"/LTS (p.ej. Ubuntu 22.04, que trae
# 1.0.18) se quedan cortas y el build falla con errores de "no declarado en
# este ambito" -- Arch (rolling release) nunca tiene este problema, asi que
# solo hace falta comprobar/compilar de repuesto para Debian/Ubuntu.
MIN_SODIUM_VERSION="1.0.19"
FALLBACK_SODIUM_VERSION="1.0.20"

ensure_recent_libsodium() {
  if [ "$DISTRO" = "arch" ]; then
    return  # pacman siempre trae la ultima estable, nunca hace falta esto
  fi

  local current=""
  if pkg-config --exists libsodium 2>/dev/null; then
    current="$(pkg-config --modversion libsodium)"
  fi

  if [ -n "$current" ] && dpkg --compare-versions "$current" ge "$MIN_SODIUM_VERSION"; then
    echo "    libsodium $current ya es suficientemente reciente (>= $MIN_SODIUM_VERSION)."
    return
  fi

  echo "==> libsodium del sistema (${current:-no encontrado}) es demasiado antiguo para esta"
  echo "    libreria (hace falta >= $MIN_SODIUM_VERSION, usa HKDF) -- compilando"
  echo "    libsodium $FALLBACK_SODIUM_VERSION propio desde el codigo fuente oficial..."
  local build_dir
  build_dir="$(mktemp -d)"
  (
    cd "$build_dir"
    curl -fsSL -o libsodium.tar.gz \
      "https://download.libsodium.org/libsodium/releases/libsodium-${FALLBACK_SODIUM_VERSION}.tar.gz"
    tar xzf libsodium.tar.gz
    cd "libsodium-${FALLBACK_SODIUM_VERSION}"
    ./configure
    make -j"$(nproc)"
    sudo make install
    sudo ldconfig
  )
  rm -rf "$build_dir"
  echo "    libsodium $FALLBACK_SODIUM_VERSION instalado en /usr/local."
}

ensure_recent_libsodium

echo "==> Configurando el proyecto con CMake (solo servidor, sin cliente de escritorio)..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DTEMPLAR_SERVER_ONLY=ON

echo "==> Compilando templar_server (puede tardar unos minutos la primera vez)..."
cmake --build "$BUILD_DIR" --target templar_server -j"$(nproc)"

SERVER_BIN="$BUILD_DIR/server/templar_server"
if [ ! -x "$SERVER_BIN" ]; then
  echo "La compilacion no genero el binario esperado en $SERVER_BIN" >&2
  exit 1
fi

# Puerto y directorio de datos por defecto (ver server/src/main.cpp):
# argv[1] = puerto (8080 si no se indica), argv[2] = ruta a la BD (dentro
# de $XDG_DATA_HOME/templar o ~/.local/share/templar si no se indica).
# Aqui solo se generan de ejemplo -- el archivo .service final es tuyo
# para ajustar antes de instalarlo.
DEFAULT_PORT=8080
SERVICE_EXAMPLE="$SCRIPT_DIR/templar-server.service.example"
RUN_USER="$(whoami)"

cat > "$SERVICE_EXAMPLE" <<EOF
# Ejemplo de unidad systemd para templar_server -- AJUSTA lo que haga
# falta (usuario, puerto, WorkingDirectory) y luego, como root:
#   cp templar-server.service.example /etc/systemd/system/templar-server.service
#   systemctl daemon-reload
#   systemctl enable --now templar-server
#
# Los datos (BD sqlite + blobs de archivos + certificado TLS autofirmado)
# se guardan en \$XDG_DATA_HOME/templar o, si no esta definido,
# ~/.local/share/templar del usuario que ejecute el proceso (aqui: $RUN_USER).
[Unit]
Description=Templar mk3 -- servidor de chat E2E
After=network.target

[Service]
Type=simple
User=$RUN_USER
ExecStart=$SERVER_BIN $DEFAULT_PORT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

echo ""
echo "Listo. El binario del servidor esta en:"
echo "    $SERVER_BIN"
echo ""
echo "Para volver a compilar tras subir una version nueva del codigo, ejecuta"
echo "este mismo script otra vez (es incremental, no hace falta borrar $BUILD_DIR)."
echo ""
echo "Se genero un ejemplo de unidad systemd en:"
echo "    $SERVICE_EXAMPLE"
echo "revisalo (usuario, puerto) y luego instalalo tu mismo como root, o"
echo "arranca el servidor a mano para probarlo primero:"
echo "    $SERVER_BIN $DEFAULT_PORT"
echo ""
echo "Nota sobre TLS: la primera vez que arranque genera un certificado"
echo "autofirmado (necesita 'openssl' en el PATH, ya instalado arriba) e"
echo "imprime su huella (fingerprint) por consola/journalctl -- compartela"
echo "con quien vaya a conectarse por primera vez si quiere verificarla a mano"
echo "(los clientes la recuerdan solos tras la primera conexion, TOFU)."
echo ""
echo "Puerto por defecto: $DEFAULT_PORT. Si tu proveedor/firewall bloquea ese"
echo "puerto, cambialo en el ExecStart de la unidad (o al arrancar a mano,"
echo "como primer argumento: $SERVER_BIN <puerto>)."
