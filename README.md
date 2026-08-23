# Templar

Chat cifrado extremo a extremo, multiusuario, mediado por servidor. Un servidor
central retransmite mensajes entre clientes pero nunca puede leerlos: todo el
contenido (texto, archivos, metadatos de grupo) se cifra en el dispositivo de
origen y solo se descifra en el de destino.

Pensado para un grupo pequeño de confianza (amigos, familia). Lo mas sencillo
es correrlo sobre una red privada (p.ej. [Tailscale](https://tailscale.com/)),
pero tambien se puede exponer directo a Internet con un dominio propio o DNS
dinamico (ver `setup_letsencrypt.sh` y "Exposicion directa a Internet" mas
abajo) -- no esta pensado como un servicio publico abierto a cualquiera.

## Caracteristicas

- **Mensajeria 1 a 1** con cifrado Double Ratchet (mismo diseño que Signal):
  cada mensaje usa una clave distinta, con forward secrecy y post-compromise
  security.
- **Arranque de conversacion via X3DH** con one-time prekeys: el primer
  mensaje a alguien nuevo no depende de que esa persona este conectada en ese
  momento, y usa una clave de un solo uso cuando hay alguna disponible.
- **Grupos**: creacion, invitacion con aceptacion explicita (nunca una
  incorporacion silenciosa), expulsion, salida voluntaria, transferencia de
  admin automatica si el admin se va. Cada mensaje de grupo se reparte
  cifrado individualmente para cada miembro (fan-out sobre canales 1 a 1
  existentes) -- el servidor jamas ve el contenido ni sabe que un mensaje
  pertenece a un grupo mas alla del identificador opaco.
- **Transferencia de archivos** (hasta 200 MB), tanto en chats 1 a 1 como en
  grupo, y con destinatarios desconectados: el archivo se sube una vez,
  cifrado, a un almacen de blobs en el servidor (que nunca ve el contenido
  en claro), y solo un puntero pequeno con la clave de descifrado viaja por
  el canal cifrado de cada destinatario -- la descarga es siempre manual (un
  clic/toque), nunca automatica. Los blobs caducan a los 30 dias. En
  Android, una foto descargada se guarda en la Galeria y el resto de
  archivos en Descargas (via MediaStore, Android 10+), visibles para
  cualquier otra app -- no solo dentro de Templar.
- **Chat contigo mismo** ("notas"), siempre la segunda conversacion de la
  lista (justo despues de "Sistema") -- es puramente local, no pasa por el
  servidor ni se cifra (no hay a quien ocultarselo), asi que funciona sin
  conexion y sobrevive a un reinicio del servidor.
- **Persistencia local cifrada**: identidad, sesiones y todo el historial se
  guardan en disco cifrados con la contrasena de la cuenta (Argon2id +
  XChaCha20-Poly1305) -- si el servidor se resetea o pierdes la conexion, el
  historial local sigue ahi.
- **Presencia en tiempo real**, contador de no leidos persistente, tema
  visual personalizable, notificaciones de bandeja del sistema (escritorio)
  o nativas (Android).
- **Busqueda dentro de una conversacion** con navegacion entre coincidencias.
- **Separadores de fecha** entre mensajes de dias distintos, calculados al
  renderizar el historial (no se guardan como lineas aparte).
- **TLS**, siempre, ademas del cifrado extremo a extremo de los mensajes.
  Por defecto con anclaje de certificado (TOFU): el cliente recuerda la
  huella la primera vez que se conecta a un servidor y avisa si cambiase mas
  adelante sin motivo -- igual que `known_hosts` de SSH. Si el servidor tiene
  un dominio propio, `setup_letsencrypt.sh` lo cambia por un certificado real
  de Let's Encrypt, que el cliente valida de la forma normal (CA publica) en
  vez de solo por huella.
- **Protecciones anti-abuso en el servidor**: limite de intentos de login y
  de registros por IP, tope de conexiones concurrentes por IP, una conexion
  que nunca completa el login se cierra sola a los 20s, cuota de
  almacenamiento de archivos por usuario, y tope de mensajes/invitaciones
  pendientes por destinatario -- pensado para poder exponer el servidor
  directo a Internet sin depender solo de la red de transporte (VPN/LAN)
  para la seguridad.

## Arquitectura

```
common/   Framing de red (Wire.hpp/Protocol.hpp) compartido por cliente, servidor y movil.
crypto/   Identidad, X3DH y Double Ratchet -- no sabe nada de red ni de Qt.
server/   Boost.Asio (coroutines) + SQLite. Enruta ciphertext opaco, nunca lo descifra.
client/   Qt6 Widgets (escritorio). UI, LocalStore, orquestacion de cripto.
phone/    Qt6 Quick/QML (Android). Reusa NetworkManager/CryptoEngine/LocalStore de client/ tal cual.
```

El servidor guarda: cuentas (usuario + hash de contrasena + claves publicas),
una cola de mensajes pendientes por si el destinatario esta desconectado
(`mailbox`), la membresia de grupos, y los blobs de archivo cifrados
(metadatos en SQLite, bytes en disco aparte). Nunca guarda texto plano ni
claves privadas de nadie.

Para una explicacion mas a fondo de como encajan las piezas -- protocolo de
red, X3DH paso a paso, Double Ratchet, LocalStore, grupos -- ver
[`docs/`](docs/).

## Requisitos

**Cliente de escritorio + servidor:**

- CMake >= 3.20, compilador con C++20 (GCC/Clang)
- Boost (headers + Asio)
- libsodium
- SQLite3
- OpenSSL (headers + CLI, para TLS)
- Qt6 (Widgets, Network, Test) -- solo hace falta si vas a compilar el
  cliente de escritorio; un servidor "de solo servidor" no lo necesita (ver
  mas abajo)

**Cliente movil (Android):** Qt6 para Android + Android SDK/NDK + JDK 17 --
ver [`phone/BUILD.md`](phone/BUILD.md) para la instalacion del entorno
(es sobre todo interactiva, no se presta a un script de un solo paso) y
[`build_mobile.sh`](build_mobile.sh) para compilar y generar el APK una vez
el entorno esta listo.

## Instalacion

**Cliente de escritorio** (Arch o Debian/Ubuntu): el script se encarga de
las dependencias, compila el proyecto (cliente + servidor) e instala un
lanzador de escritorio y el comando `templar`:

```bash
./setup.sh
```

**Solo servidor**, para desplegar en una maquina sin entorno grafico (no
necesita Qt6 en absoluto): sube la carpeta del proyecto a esa maquina y
ejecuta ahi:

```bash
./deploy_server.sh
```

Deja el binario compilado listo y un archivo `.service` de ejemplo para
`systemctl` (no lo instala solo, lo revisas e instalas tu). Ver los
comentarios del propio script para mas detalle.

Para compilar a mano en cualquier otra distro (con las dependencias ya
instaladas):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Uso

**Servidor** (una sola maquina hace de servidor; el resto se conecta a ella):

```bash
./build/server/templar_server [puerto=8080] [ruta_db] [direccion_bind=0.0.0.0]
```

Por defecto escucha en el puerto 8080 y guarda su base de datos y su
certificado TLS en `~/.local/share/templar/` (independiente del directorio
desde el que se lance). La primera vez que arranca genera un certificado
autofirmado e imprime su huella SHA-256 por consola.

**Cliente**:

```bash
templar          # si se instalo con setup.sh
./build/client/templar_client   # o directamente el binario
```

Introduce la IP/host y puerto del servidor, y regístrate. La primera vez que
te conectes a un servidor nuevo, el cliente confía en su certificado TLS y
recuerda su huella (como `known_hosts` de SSH); si esa huella cambiase en el
futuro sin que hayas reinstalado el servidor, la conexión se corta con un
aviso -- posible señal de un ataque de intermediario.

## Tests

Cada binario de test es independiente; varios necesitan un `templar_server`
real corriendo (con un archivo de base de datos vacío o desechable, para no
interferir con tu servidor real):

```bash
# Sin red:
./build/crypto/templar_crypto_tests
./build/crypto/templar_ratchet_tests
./build/client/templar_persistence_smoke
./build/client/templar_migration_smoke

# Con un servidor de prueba corriendo en el puerto 19999:
./build/server/templar_server 19999 /tmp/templar_test.db &
./build/client/templar_e2e_smoke 19999
./build/client/templar_gui_smoke 19999
./build/client/templar_group_smoke 19999

# reregister_smoke necesita DOS servidores (simula un reseteo de servidor):
./build/server/templar_server 19998 /tmp/templar_test2.db &
./build/client/templar_reregister_smoke 19999 19998
```

## Limitaciones conocidas

- **Sin sincronizacion entre dispositivos**: cada instalacion tiene su propia
  identidad criptografica. Usar la misma cuenta en dos maquinas (p.ej.
  escritorio y movil) no comparte historial ni sesiones -- cada una arranca
  con una identidad nueva, y los mensajes cifrados para la identidad vieja
  de esa cuenta dejan de poder descifrarse ahi hasta reiniciar el
  intercambio de claves con cada contacto. Anadir soporte de verdad para
  varios dispositivos por cuenta implicaria repensar el modelo de
  identidad/sesiones (una identidad distinta por dispositivo, con
  reparto/fan-out por dispositivo ademas de por miembro de grupo).
- **Sin editar ni borrar mensajes ya enviados.**
- **Registro sigue siendo abierto** (con límite de tasa por IP, pero sin
  invitación ni aprobación) -- suficiente para un grupo pequeño no publicado
  en ningún sitio, pero no es control de acceso real. Tampoco hay ninguna
  herramienta de administración (banear/borrar una cuenta ya creada).

## Exposición directa a Internet

El servidor puede correr sin una VPN/LAN de por medio -- ver
[`setup_letsencrypt.sh`](setup_letsencrypt.sh) para provisionar un
certificado TLS real (Let's Encrypt) sobre un dominio propio o DNS dinámico
(DuckDNS, etc.), y las protecciones anti-abuso listadas en
"Características" (límites de login/registro por IP, tope de conexiones,
cuotas de almacenamiento). Ver [docs/03-servidor.md](docs/03-servidor.md#límites-anti-abuso)
para el detalle de cada mecanismo. Sigue sin ser un servicio pensado para
uso público general (ver limitación de arriba sobre el registro abierto).

## Estructura del repositorio

```
common/include/templar/       Wire.hpp (framing binario), Protocol.hpp (tipos de mensaje)
common/src/
crypto/include/templar/crypto/ Identity.hpp, X3DH.hpp, DoubleRatchet.hpp, FileCrypto.hpp
crypto/src/
crypto/tests/                  Tests de cripto pura, sin red ni Qt
server/src/                    main.cpp, Database, BlobStore, Router, Session
client/src/                    main.cpp, MainWindow, NetworkManager, CryptoEngine, LocalStore, ...
client/tests/                  Tests de integracion (algunos manejan la GUI real)
phone/src/                     main.cpp, ClientController, ConversationListModel, ChatHistoryModel
phone/qml/                     Paginas y componentes Qt Quick
phone/android/                 Manifest, iconos, clases Java propias (notificaciones, camara, ...)
docs/                          Explicacion a fondo del protocolo, cripto, grupos, etc.
setup.sh                       Instalador del cliente de escritorio (Arch/Debian)
deploy_server.sh               Instalador de solo servidor, sin Qt6
setup_letsencrypt.sh           Certificado TLS real (Let's Encrypt) para exposicion a Internet
build_mobile.sh                Compila el cliente movil y genera el APK de Android
```
