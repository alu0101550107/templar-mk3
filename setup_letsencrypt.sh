#!/usr/bin/env bash
# Templar mk3 -- sustituye el certificado TLS autofirmado (TOFU) de una
# instalacion YA desplegada con deploy_server.sh por uno real de Let's
# Encrypt, para poder exponer el servidor directo a Internet (con un
# dominio/subdominio propio -- p.ej. gratis via DuckDNS) en vez de solo
# sobre una VPN/LAN de confianza.
#
# Requisitos antes de correr esto:
#   - deploy_server.sh ya corrido, y el templar-server.service de ejemplo
#     que genera ya instalado (systemctl enable --now templar-server) --
#     este script necesita poder reiniciarlo.
#   - Un hostname que resuelva a la IP publica de esta maquina (DuckDNS,
#     dominio propio con DNS dinamico, lo que sea) -- Let's Encrypt valida
#     el dominio conectandose el mismo a este servidor.
#   - Puerto 80 reenviado desde el router hasta esta maquina (SOLO para el
#     reto de Let's Encrypt -- Templar en si sigue usando su puerto de
#     siempre, 8080 por defecto, sin relacion con el 80).
#
# Uso: sudo ./setup_letsencrypt.sh <tu-subdominio.duckdns.org> <tu@email>
#
# Se puede volver a ejecutar sin problema (p.ej. para cambiar de dominio);
# certbot es idempotente sobre certificados ya emitidos.

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Hace falta root (certbot escribe en /etc/letsencrypt y hay que" >&2
  echo "leer la clave privada resultante para copiarla) -- ejecuta con sudo." >&2
  exit 1
fi

if [ $# -ne 2 ]; then
  echo "Uso: sudo $0 <tu-subdominio.duckdns.org> <tu@email>" >&2
  exit 1
fi
DOMAIN="$1"
EMAIL="$2"

SERVICE_NAME="templar-server.service"
if ! systemctl list-unit-files "$SERVICE_NAME" >/dev/null 2>&1; then
  echo "No se encontro $SERVICE_NAME -- corre deploy_server.sh e instala el" >&2
  echo "templar-server.service.example que genera antes de esto." >&2
  exit 1
fi

# El usuario que corre el servicio decide donde vive su data dir (mismo
# criterio XDG que server/src/main.cpp::defaultDataDir) -- se lee del
# propio .service en vez de asumir nada, para no desincronizarse si se
# edito el User= a mano tras generarlo.
RUN_USER="$(systemctl show -p User --value "$SERVICE_NAME")"
if [ -z "$RUN_USER" ]; then
  echo "No se pudo determinar que usuario corre $SERVICE_NAME (User= vacio" >&2
  echo "en la unidad) -- revisala a mano." >&2
  exit 1
fi
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
DATA_DIR="${XDG_DATA_HOME:-$RUN_HOME/.local/share}/templar"

echo "==> Detectando distribucion..."
if command -v pacman >/dev/null 2>&1; then
  DISTRO="arch"
elif command -v apt-get >/dev/null 2>&1; then
  DISTRO="debian"
else
  echo "No se reconoce el gestor de paquetes (Arch/pacman o Debian-Ubuntu/apt)." >&2
  echo "Instala 'certbot' manualmente y vuelve a correr este script." >&2
  exit 1
fi
echo "    Detectado: $DISTRO"

if ! command -v certbot >/dev/null 2>&1; then
  echo "==> Instalando certbot..."
  if [ "$DISTRO" = "arch" ]; then
    pacman -Sy --needed --noconfirm certbot
  else
    apt-get update
    apt-get install -y certbot
  fi
fi

# copy_and_restart(): vuelca fullchain.pem/privkey.pem de la emision mas
# reciente de $DOMAIN a donde ya los lee ensureServerCertificate() en
# server/src/main.cpp (main.cpp solo genera uno autofirmado si NO
# encuentra ya cert.pem/key.pem ahi -- nunca pisa uno real que ya exista),
# con permisos para RUN_USER, y reinicia el servicio para que los recoja.
# fullchain.pem (NO el cert.pem de certbot, que es solo la hoja sin la
# cadena intermedia) es imprescindible: un cliente sin la intermedia en
# cache fallaria la validacion contra un cert.pem de solo-hoja.
copy_and_restart() {
  local live_dir="/etc/letsencrypt/live/$DOMAIN"
  install -d -m 700 -o "$RUN_USER" "$DATA_DIR"
  install -m 644 -o "$RUN_USER" "$live_dir/fullchain.pem" "$DATA_DIR/cert.pem"
  install -m 600 -o "$RUN_USER" "$live_dir/privkey.pem" "$DATA_DIR/key.pem"
  systemctl restart "$SERVICE_NAME"
}

echo "==> Pidiendo el certificado a Let's Encrypt para $DOMAIN..."
echo "    (necesita el puerto 80 libre y reenviado hasta aqui un instante)"
certbot certonly --standalone -d "$DOMAIN" -m "$EMAIL" --agree-tos --non-interactive

copy_and_restart
echo "    Certificado instalado en $DATA_DIR y $SERVICE_NAME reiniciado."

# Hook de renovacion: certbot ya instala su propio timer systemd (corre
# certbot.timer dos veces al dia, solo renueva de verdad cerca de la
# caducidad) -- esto solo anade el paso de "avisar a Templar" cada vez que
# ese timer renueve algo de verdad.
HOOK_PATH="/etc/letsencrypt/renewal-hooks/deploy/templar.sh"
mkdir -p "$(dirname "$HOOK_PATH")"
cat > "$HOOK_PATH" <<EOF
#!/usr/bin/env bash
# Generado por setup_letsencrypt.sh -- copia el certificado renovado a
# donde Templar lo espera y reinicia el servicio. Certbot llama a esto
# solo, tras cada renovacion real (no en cada chequeo del timer).
set -euo pipefail
install -d -m 700 -o "$RUN_USER" "$DATA_DIR"
install -m 644 -o "$RUN_USER" "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" "$DATA_DIR/cert.pem"
install -m 600 -o "$RUN_USER" "/etc/letsencrypt/live/$DOMAIN/privkey.pem" "$DATA_DIR/key.pem"
systemctl restart "$SERVICE_NAME"
EOF
chmod +x "$HOOK_PATH"

echo ""
echo "Listo. $SERVICE_NAME ya esta sirviendo con un certificado real de"
echo "Let's Encrypt para $DOMAIN -- las renovaciones futuras (certbot.timer,"
echo "ya activo tras instalar el paquete) se aplican solas via $HOOK_PATH."
echo ""
echo "Comprueba conectando el cliente contra $DOMAIN: ya no deberia hacer"
echo "falta confiar en ningun certificado la primera vez (TOFU), solo si el"
echo "certificado tuviera algun problema real veras un error de conexion."
