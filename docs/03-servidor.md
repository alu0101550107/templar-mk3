# 03 -- El servidor: Boost.Asio, SQLite, TLS

Archivos: `server/src/{main,Database,Router,Session}.{hpp,cpp}`.

El servidor no sabe nada de criptografia de aplicacion -- para el, todo
mensaje es un blob de bytes opaco que enruta entre cuentas. Lo que si tiene
que resolver es: aceptar muchas conexiones a la vez sin bloquearse, guardar
cuentas y una cola de mensajes pendientes, y (desde la fase de seguridad)
cifrar el transporte con TLS.

## Boost.Asio con corutinas: lo minimo para entender `Session::run()`

Boost.Asio es una libreria de I/O asincrono. La forma "clasica" de usarla es
con callbacks anidados, que se vuelve ilegible rapido. Este proyecto usa la
variante moderna: **corutinas de C++20** (`asio::awaitable<void>`,
`co_await`, `co_spawn`), que dejan escribir codigo asincrono con pinta de
codigo secuencial normal.

```cpp
asio::awaitable<void> Session::run() {
  std::array<uint8_t, 4096> buf;
  for (;;) {
    size_t n = co_await socket_.async_read_some(asio::buffer(buf), asio::use_awaitable);
    // ... procesar los n bytes que llegaron ...
  }
}
```

Lee como un bucle bloqueante normal, pero `co_await` en realidad **suspende**
esta funcion sin bloquear el hilo -- mientras espera datos de ESTE socket,
el mismo hilo puede estar atendiendo a otras cien conexiones. Cuando llegan
datos, la corutina se reanuda justo donde se quedo. `Session::start()`
(`Session.cpp:19-22`) es quien la lanza: `co_spawn` la registra en el
`io_context` y vuelve inmediatamente, sin esperar a que `run()` termine (que
no termina hasta que esa conexion se cierra).

`main.cpp` arranca un pool de hilos (`numThreads = max(2, hardware_concurrency())`)
todos corriendo `ioContext.run()` -- Boost.Asio reparte las corutinas listas
para reanudarse entre esos hilos. Esto es importante para lo que viene
despues.

## El strand: por que `deliver()` no necesita un mutex

Si varios hilos pueden estar atendiendo la misma conexion (dos mensajes de
DOS remitentes distintos que quieren entregarle algo a la misma persona, a
la vez), hace falta serializar el acceso al socket de esa persona -- sin
eso, dos escrituras podrian solaparse y corromper el framing. Boost.Asio da
una herramienta para esto: un **strand**, que garantiza que el codigo
posteado a el nunca se ejecuta en paralelo consigo mismo, aunque el
`io_context` tenga varios hilos.

Cada `Session` se crea sobre su propio strand (`main.cpp`, dentro de
`listener()`: `asio::make_strand(ioContext.get_executor())`). `deliver()`
(`Session.cpp:24-37`) usa `asio::post(socket_.get_executor(), ...)` para
saltar a ese strand antes de tocar la cola de escritura -- asi, aunque
`deliver()` se llame desde el hilo que esta atendiendo la conexion de
CUALQUIER otro usuario, la modificacion real de `writeQueue_` siempre
ocurre en el mismo strand, uno detras de otro, sin carreras. Por eso no hay
ningun `std::mutex` en `Session`.

### La historia real de un bug: la cola de escritura

Boost.Asio tiene una regla estricta: **nunca puede haber mas de un
`async_write()` en vuelo a la vez sobre el mismo socket**. Si la llamas dos
veces seguidas antes de que la primera termine, el comportamiento no esta
definido -- en la practica, los dos escritos pueden intercalarse y
corromper el framing del receptor.

La primera version de `deliver()` llamaba a `async_write()` directamente
cada vez. Funcionaba bien para mensajes de texto sueltos, porque en la
practica nunca coincidian dos a la vez. Pero al implementar la
transferencia de archivos (fase 5), un archivo se manda en decenas de
bloques de 64 KB **seguidos, muy rapido** -- y ahi si que era facil que el
segundo bloque se mandara antes de que el `async_write()` del primero
hubiera terminado. El sintoma real que reporto el usuario: al mandar un
archivo, el receptor se desconectaba solo, sin poder volver a loguear.

El arreglo es el patron estandar de "cola de escritura" (el mismo que usa
el ejemplo oficial `chat_server.cpp` de Boost.Asio), y es exactamente lo que
hay ahora en `Session.cpp:24-53`:

```cpp
void Session::deliver(MsgType type, const Bytes& payload) {
  auto frame = std::make_shared<Bytes>(encodeFrame(type, payload));
  asio::post(socket_.get_executor(), [self = shared_from_this(), frame] {
    bool writeInProgress = !self->writeQueue_.empty();
    self->writeQueue_.push_back(frame);
    if (!writeInProgress) self->doWrite();   // solo arranca si no habia ya uno en curso
  });
}

void Session::doWrite() {
  asio::async_write(socket_, asio::buffer(*writeQueue_.front()),
    [self = shared_from_this()](boost::system::error_code ec, size_t) {
      if (ec) return;
      self->writeQueue_.pop_front();
      if (!self->writeQueue_.empty()) self->doWrite();  // encadena el siguiente
    });
}
```

Cada `deliver()` se limita a encolar; solo se arranca un `async_write` nuevo
si no habia ya uno en curso, y el propio *completion handler* de
`doWrite()` es quien encadena el siguiente. Nunca hay dos en vuelo a la vez.

Es un buen ejemplo de por que "funciona en mis pruebas" no es lo mismo que
"es correcto": el bug solo se manifestaba con rafagas rapidas de mensajes
seguidos, que un simple intercambio de texto nunca genera. El test de
regresion que lo cubre (`client/tests/gui_smoke.cpp`) manda a proposito un
archivo de 2 MB (~32 bloques) en vez de uno pequeño, con un comentario
explicando por que: un archivo de un solo bloque no habria detectado nunca
esta clase de bug.

## Database: SQLite envuelto a mano

`server/src/Database.hpp`/`.cpp`. Sin ORM -- una clase `Stmt` pequeña
(RAII sobre `sqlite3_stmt*`, para que un `sqlite3_finalize` nunca se olvide
ni siquiera si salta una excepcion a mitad de la consulta) mas funciones
libres `bindText`/`bindBlob`/`columnBlob`/`columnText` para no repetir el
mismo boilerplate en cada metodo.

Todos los metodos publicos toman `std::lock_guard<std::mutex> lock(mutex_)`
al entrar -- un unico mutex serializa TODO acceso a la base de datos. Para
el volumen de un chat de amigos es mas que suficiente, y evita la
complejidad de un hilo dedicado a DB (que si valdria la pena si esto
creciera mucho). El comentario de cabecera de la clase lo explica.

Tablas relevantes (ver el `CREATE TABLE` en el constructor):

- `users`: username, hash de contrasena (nunca la contrasena en si), y las
  claves publicas necesarias para X3DH (identidad, signed prekey + firma).
- `mailbox`: la cola de mensajes pendientes de entrega. Cada fila es un
  mensaje cifrado para un `recipient_id` concreto; se borra en cuanto el
  cliente manda un `Ack` confirmando que lo proceso (`ackMessage`). Esto es
  lo que permite mandarle un mensaje a alguien desconectado: se queda aqui
  hasta que se conecte (`flushMailbox`, en `Router`).
- `one_time_prekeys`: pool de OTPK publicas por usuario, cada una se
  entrega como mucho una vez (`takeOneTimePrekey` la borra al leerla).
- `groups`/`group_members`/`group_invites`: ver [05-grupos.md](05-grupos.md).

## Router: la logica de aplicacion

`server/src/Router.hpp`/`.cpp`. `handleFrame()` es un `switch` grande que
despacha cada `MsgType` a un metodo privado (`handleLogin`, `handleSendMsg`,
...). Todo esta envuelto en un `try/catch` a este nivel -- un handler
individual no necesita su propio try/catch, cualquier excepcion que se le
escape simplemente se loguea y esa conexion sigue viva para el siguiente
frame.

Dos estructuras en memoria (no en la base de datos, a proposito):

- `online_`: `username -> weak_ptr<Session>` de quien esta conectado ahora
  mismo. Se usa para decidir si un mensaje se puede entregar en vivo o solo
  se queda en la cola.
- `presenceSubscribers_`: quien quiere que le avisen si tal usuario se
  conecta/desconecta. Se pierde al reconectar a proposito -- no hay ningun
  concepto de "lista de contactos" persistente en el servidor.

El patron que se repite en casi todos los `handleX`: validar, tocar la
`Database`, y si el destinatario esta online (`findOnline()`), empujarle el
resultado con `session->deliver(...)`; si no, ya quedo guardado en la DB
para su proximo login.

## Límites anti-abuso

Todos en memoria (se olvidan si el servidor se reinicia, a proposito -- el
objetivo es frenar abuso activo, no mantener una lista negra permanente), y
pensados especificamente para poder exponer el servidor directo a Internet
(ver [README.md](../README.md#exposición-directa-a-internet)) sin depender
solo de estar detras de una VPN/LAN de confianza.

### Login y registro

`handleLogin` comprueba `isLoginLocked(ip)` antes incluso de tocar la base
de datos. `LoginAttempts` (`Router.hpp`) es un mapa en memoria,
`ip -> {contador de fallos, cuando empezo la ventana, hasta cuando esta
bloqueado}`, protegido por su propio mutex. Se limita por **IP** y no por
username: si se limitara por username, probar muchos usuarios distintos
desde la misma maquina se saltaria el limite. Al quinto fallo en 5 minutos,
esa IP queda bloqueada 30 segundos.

`handleRegister` usa un mecanismo hermano pero mas simple: como una cuenta
legitima solo se registra una vez, no hace falta distinguir exito de fallo
como en el login -- basta con contar CUALQUIER intento (`registerAttemptsByIp_`)
en una ventana deslizante de 60 minutos, sin timer de bloqueo aparte. Al
quinto intento desde la misma IP en esa hora, el sexto se rechaza.

### Conexiones concurrentes y timeout pre-autenticacion

`Router::tryRegisterConnection(ip)`/`unregisterConnection(ip)` llevan la
cuenta de cuantas conexiones tiene abiertas cada IP a la vez
(`connectionsByIp_`, tope 20) -- `listener()` (`main.cpp`) lo comprueba
justo despues del `accept()`, ANTES del handshake TLS, para rechazar lo
mas barato posible. `Router::onDisconnect` decrementa el contador siempre,
sea cual sea el motivo de la desconexion.

Aparte, cada `Session` tiene su propio `preAuthTimer_`
(`Session.hpp`/`.cpp`): si una conexion completa el handshake TLS pero
nunca manda `Login`/`Register`, se cierra sola a los 20 segundos. Esto NO
es un timeout de inactividad general -- una sesion YA logueada puede estar
horas en silencio de forma legitima (solo recibiendo mensajes push), asi
que el timer se cancela para siempre en cuanto `setUsername()` confirma un
login/registro exitoso. Sin este mecanismo, alguien podria abrir conexiones
sin fin sin autenticarse nunca, cada una ocupando un socket/`Session`
indefinidamente.

### Cuotas de almacenamiento

- **Blobs de archivo**: ademas del limite por blob (`kMaxBlobBytes`, 200 MB),
  `BlobStore::beginUpload` suma el tamaño REAL en disco (no la columna
  `size` de la BD, que es solo el tamaño declarado y por tanto falseable) de
  todos los blobs de un usuario, y rechaza si superaria su cuota total
  (`kMaxBlobBytesPerUser`, 2 GB) -- incluye subidas a medio terminar, que ya
  ocupan disco de verdad.
- **Buzon e invitaciones pendientes**: `Database::enqueueMessage`/
  `enqueueGroupInvite` comprueban cuantas filas ya tiene pendientes ese
  destinatario antes de insertar una mas (`kMaxPendingMailboxPerRecipient`
  300, `kMaxPendingInvitesPerRecipient` 50) -- sin esto, una cuenta legitima
  (o comprometida) podria llenar sin limite el buzon de otra persona.
  `handleSendMsg`/`handleSendGroupMsg` tambien acotan el tamaño de un
  mensaje individual (`kMaxChatMessageBytes`, 1 MB) para que ese tope de
  filas siga significando algo en bytes totales.
- **One-time prekeys**: `handlePublishOtpk` ya limitaba cada LLAMADA a
  200 claves, pero nada impedia llamarlo en bucle sin fin -- `addOneTimePrekeys`
  ahora tambien acota el total acumulado por usuario (`kMaxOneTimePrekeysPerUser`,
  500), descartando en silencio lo que sobre.

## TLS: por que y como

Todo lo anterior viaja sobre una conexion cifrada por transporte, ademas
del cifrado extremo a extremo de los mensajes en si. Son dos capas
independientes con propositos distintos: el cifrado E2E protege el
CONTENIDO incluso del propio servidor; TLS protege que alguien en medio de
la red (un router comprometido, otro dispositivo en la misma LAN) pueda ver
siquiera los METADATOS de la conexion (quien se conecta, con que
frecuencia, tamaños de mensaje) o inyectar/alterar bytes en transito.

`main.cpp` envuelve el `tcp::socket` aceptado en un
`asio::ssl::stream<tcp::socket>` y hace el *handshake* TLS antes de crear el
`Session` (`listener()`, `main.cpp`). `Session` en si apenas cambia: como
`ssl::stream` implementa la misma interfaz de lectura/escritura asincrona
que un socket normal, `async_read_some`/`async_write` funcionan igual --
solo el tipo del socket cambia (`Session::SslStream`), y un par de sitios
que necesitaban el socket TCP de verdad (para leer la IP remota, o para
cerrar la conexion) usan `.lowest_layer()` para bajar a el.

### El certificado autofirmado y la confianza TOFU

No hay una autoridad certificadora publica para un servidor casero, asi que
el certificado es autofirmado: `main.cpp::ensureServerCertificate()` lo
genera con el `openssl` del sistema la primera vez que arranca (si ya
existe, no hace nada) y lo guarda junto a la base de datos.

Un certificado autofirmado no sirve de nada si el cliente lo acepta sin
comprobar nada -- eso permitiria a cualquiera en medio de la red
sustituirlo por el suyo. La solucion que usa este proyecto es **TOFU**
(*trust on first use*), el mismo criterio que usa `known_hosts` de SSH: la
PRIMERA vez que el cliente habla con un `host:puerto`, calcula la huella
SHA-256 del certificado que le presento y la guarda en un archivo local de
texto plano (`NetworkManager::checkAndRememberCertificate`,
`client/src/NetworkManager.cpp`). En cualquier conexion FUTURA a ese mismo
`host:puerto`, si la huella no coincide con la guardada, se corta la
conexion de inmediato y se avisa -- podria ser un ataque de intermediario, o
simplemente que reinstalaste el servidor (en cuyo caso hay que borrar esa
linea del archivo a mano).

No es tan fuerte como una CA publica (la primerisima conexion se confia a
ciegas), pero es exactamente el mismo modelo de confianza que ya usas cada
vez que te conectas por SSH a un servidor nuevo, y es apropiado para el
caso de uso: un grupo pequeño donde la primera conexion normalmente pasa
por un canal donde ya confias (te pasan la IP y el puerto directamente).

Esto es solo el camino por defecto (sin dominio, pensado para VPN/LAN). Si
el servidor tiene un dominio o subdominio propio (p.ej. DNS dinamico tipo
DuckDNS) y esta expuesto directamente a Internet, `setup_letsencrypt.sh`
sustituye ese certificado autofirmado por uno real de Let's Encrypt.
`NetworkManager::onSslErrors` distingue ambos casos por
`QSslCertificate::isSelfSigned()`: solo ignora los errores de TLS cuando el
certificado es de verdad autofirmado (el caso de arriba); si viene de una
CA reconocida pero tiene algun problema real (caducado, dominio que no
coincide, revocado...) ya no se ignora, se corta la conexion como haria
cualquier cliente HTTPS normal. El TOFU de `checkAndRememberCertificate`
sigue corriendo en ambos casos, como capa extra.

El `sslContext` en si (`main.cpp`) tambien fija una version minima --
`no_sslv2`/`no_sslv3`/`no_tlsv1`/`no_tlsv1_1` via `set_options()`, dejando
solo TLS 1.2/1.3 -- en vez de depender de que la politica de OpenSSL del
sistema operativo excluya por su cuenta los protocolos viejos/rotos.
