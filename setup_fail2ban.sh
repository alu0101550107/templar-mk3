#!/usr/bin/env bash
# Templar mk3 -- instala y configura fail2ban para banear por firewall a
# quien insista con logins invalidos o intente saltarse el rate-limit de
# registro contra un templar-server ya desplegado (deploy_server.sh) y
# expuesto directo a Internet.
#
# El rate-limit propio de la app (Router.hpp: LoginAttempts/registerAttemptsByIp_)
# ya RETRASA a un atacante, pero es en memoria (se olvida si el servidor se
# reinicia) y nunca corta el trafico a nivel de red -- fail2ban vigila el
# log del servicio (via journald) y, si una IP repite el patron de "Login
# fallido"/"Registro rechazado" mas de la cuenta, la bloquea de verdad en
# el firewall durante un tiempo. El umbral aqui (10 en 10 min) esta puesto
# por ENCIMA del limite interno de la app (5) a proposito: solo salta para
# algo claramente insistente/automatizado, nunca para alguien que se
# equivoca de contrasena un par de veces.
#
# Requisitos: templar-server.service ya instalado (deploy_server.sh) y
# corriendo bajo systemd, con firewalld como gestor de firewall activo
# (si usas otro, cambia BANACTION mas abajo -- ver comentario).
#
# Uso: sudo ./setup_fail2ban.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Hace falta root (instala paquetes y escribe en /etc/fail2ban) -- ejecuta con sudo." >&2
  exit 1
fi

if ! systemctl list-unit-files templar-server.service >/dev/null 2>&1; then
  echo "No se encontro templar-server.service -- corre deploy_server.sh e instala el" >&2
  echo "templar-server.service.example que genera antes de esto." >&2
  exit 1
fi

# Accion de baneo para firewalld (ver systemctl list-units --type=service | grep -i firewall
# si no estas seguro de cual usas). Si tu servidor usa ufw en vez de
# firewalld, cambia esto a "ufw" y el "backend"/filtro no necesitan tocarse.
BANACTION="firewallcmd-ipset"

echo "==> Detectando distribucion..."
if command -v pacman >/dev/null 2>&1; then
  DISTRO="arch"
elif command -v apt-get >/dev/null 2>&1; then
  DISTRO="debian"
else
  echo "No se reconoce el gestor de paquetes (Arch/pacman o Debian-Ubuntu/apt)." >&2
  echo "Instala 'fail2ban' manualmente y vuelve a correr este script." >&2
  exit 1
fi
echo "    Detectado: $DISTRO"

if ! command -v fail2ban-client >/dev/null 2>&1; then
  echo "==> Instalando fail2ban..."
  if [ "$DISTRO" = "arch" ]; then
    pacman -Sy --needed --noconfirm fail2ban
  else
    apt-get update
    apt-get install -y fail2ban
  fi
fi

echo "==> Escribiendo los filtros (que cuenta como intento malo)..."
mkdir -p /etc/fail2ban/filter.d

# Fuerza bruta de login / abuso del rate-limit de registro -- umbral
# generoso (10 en 10 min), pensado para no banear nunca a alguien que
# solo se equivoca de contrasena.
cat > /etc/fail2ban/filter.d/templar.conf <<'EOF'
# Generado por setup_fail2ban.sh -- formato de log estable, ver
# server/src/Router.cpp (handleLogin/handleRegister).
[Definition]
failregex = ^.*\[Servidor\] Login fallido desde <HOST>\s*$
            ^.*\[Servidor\] Registro rechazado \(limite de tasa\) desde <HOST>\s*$
ignoreregex =
EOF

# Flood/escaneo de conexiones -- esta linea SOLO aparece si una IP ya
# supero el tope de 20 conexiones concurrentes (Router::kMaxConnectionsPerIp),
# algo que ningun cliente legitimo hace nunca -- por eso el umbral es tan
# bajo (2 en vez de 10): si aparece, ya es senal suficiente por si sola.
cat > /etc/fail2ban/filter.d/templar-flood.conf <<'EOF'
# Generado por setup_fail2ban.sh -- ver main.cpp (listener()).
[Definition]
failregex = ^.*\[Servidor\] Demasiadas conexiones concurrentes desde <HOST>, rechazada\.\s*$
ignoreregex =
EOF

echo "==> Escribiendo las celdas (umbral, ventana, duracion del baneo)..."
mkdir -p /etc/fail2ban/jail.d
cat > /etc/fail2ban/jail.d/templar.conf <<EOF
# Generado por setup_fail2ban.sh
[templar]
enabled      = true
backend      = systemd
journalmatch = _SYSTEMD_UNIT=templar-server.service
filter       = templar
maxretry     = 10
findtime     = 10m
bantime      = 1h
banaction    = $BANACTION
action       = %(action_)s

[templar-flood]
enabled      = true
backend      = systemd
journalmatch = _SYSTEMD_UNIT=templar-server.service
filter       = templar-flood
maxretry     = 2
findtime     = 5m
bantime      = 6h
banaction    = $BANACTION
action       = %(action_)s
EOF

echo "==> Validando que los filtros reconocen el formato de log real..."
if command -v fail2ban-regex >/dev/null 2>&1; then
  fail2ban-regex systemd-journal /etc/fail2ban/filter.d/templar.conf \
    --journalmatch="_SYSTEMD_UNIT=templar-server.service" || true
  fail2ban-regex systemd-journal /etc/fail2ban/filter.d/templar-flood.conf \
    --journalmatch="_SYSTEMD_UNIT=templar-server.service" || true
fi

systemctl enable --now fail2ban
systemctl restart fail2ban

echo ""
echo "Listo. Comprueba el estado de las dos celdas con:"
echo "    sudo fail2ban-client status templar"
echo "    sudo fail2ban-client status templar-flood"
echo ""
echo "Para desbanear una IP a mano si hace falta (p.ej. te bloqueaste tu mismo"
echo "probando):"
echo "    sudo fail2ban-client set templar unbanip <IP>"
echo "    sudo fail2ban-client set templar-flood unbanip <IP>"
echo ""
echo "Nota: el puerto 80 no necesita ninguna celda -- no hay ningun servicio"
echo "escuchando ahi de forma permanente (solo certbot un instante, cerca de"
echo "cada renovacion), asi que no hay ningun log de 'intento malo' que vigilar."
echo ""
echo "Bonus: si tienes SSH expuesto tambien, fail2ban ya trae de fabrica un"
echo "filtro para el (jail 'sshd') -- compueba si ya esta activo:"
echo "    sudo fail2ban-client status sshd"
