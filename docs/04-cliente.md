# 04 -- El cliente: Qt, MainWindow, LocalStore

Archivos principales: `client/src/{MainWindow,NetworkManager,CryptoEngine,
LocalStore,MessagePayload}.{hpp,cpp}`.

## Señales y slots de Qt, en dos parrafos

Si nunca has usado Qt: el patron central es "señales y slots". Un objeto
`QPushButton` **emite** una señal (`clicked()`) cuando lo pulsan. En
cualquier otro sitio del programa puedes **conectar** esa señal a un
**slot** (un metodo normal, marcado `private slots:` en la clase) para que
se llame automaticamente cuando la señal se dispare:

```cpp
connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
```

Es como un callback tipado y verificado en tiempo de compilacion, pero
desacoplado: `QPushButton` no sabe nada de `MainWindow`, ni le importa
quien (si alguien) esta escuchando. Todo el codigo de este proyecto que
reacciona a algo -- un clic, datos que llegan por red, un timer -- esta
organizado asi. El constructor de `MainWindow` (`MainWindow.cpp:45-101`) es
basicamente una lista larga de estos `connect()`, cableando cada boton/señal
a su slot.

## La forma del archivo: dos pantallas en un `QStackedWidget`

`MainWindow` tiene un `QStackedWidget` con dos paginas (`buildLoginPage()` y
`buildChatPage()`) -- solo una visible a la vez, se cambia con
`stack_->setCurrentWidget(...)` al loguear/desconectar. Vale la pena leer
esas dos funciones primero: construyen literalmente todos los widgets y
dejan claro que existe en la interfaz antes de mirar la logica que los
mueve.

## NetworkManager: el socket, envuelto

`client/src/NetworkManager.hpp`/`.cpp`. Une dos cosas: un `QSslSocket` (la
conexion TLS de verdad) y un `FrameParser` (el mismo de
[01-protocolo-de-red.md](01-protocolo-de-red.md), reusado tal cual).

El flujo de datos ENTRANTES:

```cpp
void NetworkManager::onReadyRead() {
  QByteArray chunk = socket_.readAll();
  parser_.feed(...);
  while (auto frame = parser_.next()) {
    emit frameReceived(frame->type, frame->payload);
  }
}
```

`onReadyRead()` es un slot conectado a `QSslSocket::readyRead` -- Qt lo
llama automaticamente cada vez que hay bytes nuevos en el socket. El
`while` es necesario por la misma razon que en el servidor: puede haber
llegado mas de un frame completo de golpe.

`connectToServer()` no dispara `connected()` en cuanto el TCP conecta --
espera al *handshake* TLS completo Y a que la comprobacion TOFU pase
(`onEncrypted()`, ver [03-servidor.md](03-servidor.md) para el porque). Solo
entonces emite la señal `connected()` que el resto del programa escucha.

## CryptoEngine: el puente entre la cripto pura y la red

`client/src/CryptoEngine.hpp`/`.cpp`. Mantiene la `Identity` propia y un
mapa `peer -> DoubleRatchet` (`sessions_`) con las conversaciones ya
establecidas. Cuatro metodos publicos, dos pares:

- `encryptFirst`/`decryptFirst`: el primer mensaje de una conversacion
  nueva -- ejecutan X3DH por dentro y dejan la sesion creada.
- `encryptNext`/`decryptNext`: mensajes siguientes de una sesion que ya
  existe -- van directos a `DoubleRatchet::encrypt`/`decrypt`.

`MainWindow` decide cual usar mirando `crypto_.hasSession(peer)` antes de
mandar cualquier cosa (ver `onSendClicked`, `MainWindow.cpp`). Este objeto
no sabe nada de Qt ni de sockets -- solo cifra/descifra bytes dado un
nombre de usuario como clave. Es el que hace de "traductor" entre lo que
llega/sale por `NetworkManager` (bytes+metadatos de protocolo) y lo que
entiende `crypto/` (bundles, mensajes de ratchet).

## MessagePayload: que hay DENTRO del plaintext

`client/src/MessagePayload.hpp`/`.cpp`. El Double Ratchet cifra `Bytes`
crudos sin saber que significan -- pero un mensaje real necesita distinguir
"esto es texto" de "esto es un trozo de un archivo". `MessagePayload` es un
framing pequeño e interno (nada que ver con el framing de red) que envuelve
el plaintext ANTES de cifrarlo:

```cpp
enum class PayloadKind : uint8_t { Text = 0, FileMeta = 1, FileChunk = 2, FileEnd = 3 };
```

`MessagePayload::encodeText("hola")` produce los bytes que de verdad se le
pasan a `crypto_.encryptNext(...)`; al otro lado, tras descifrar,
`MessagePayload::decode(plaintext)` dice de que tipo era y da acceso a los
campos correspondientes (`decoded.text`, `decoded.filename`, `decoded.chunk`...).
Asi, transferir un archivo reusa exactamente el mismo canal cifrado que un
mensaje de texto -- para el Double Ratchet y para el servidor, un
`FileChunk` es indistinguible de cualquier otro blob cifrado.

## LocalStore: SQLite en memoria, cifrado a disco

`client/src/LocalStore.hpp`/`.cpp`. Esta es la pieza que hace que cerrar la
app y volver a abrirla no pierda nada. Vale la pena entender el mecanismo
porque es poco habitual:

1. Al desbloquear (`unlock(username, password)`), se deriva una clave de 32
   bytes de la contrasena via **Argon2id** (`crypto_pwhash`,
   `deriveKey()`) -- lento a proposito, para que probar contraseñas por
   fuerza bruta sobre el archivo robado sea caro.
2. Si ya existe un archivo en disco para ese username, se lee, se
   **descifra** (XChaCha20-Poly1305 con esa clave) y el resultado -- una
   base de datos SQLite completa serializada -- se carga con
   `sqlite3_deserialize` **en memoria**. A partir de ahi, todas las
   consultas (`appendHistoryLine`, `saveSession`, ...) son SQL normal contra
   esa base de datos en RAM.
3. Tras cada mutacion, `persistToDisk()` vuelve a serializar la base de
   datos entera (`sqlite3_serialize`), la cifra, y la escribe a disco de
   forma atomica (archivo temporal + `rename`, para que un corte de luz a
   mitad de escritura no deje el archivo corrupto).

Tablas: `identity` (las claves privadas de X3DH), `sessions` (el estado
serializado de cada `DoubleRatchet`, uno por peer), `history` (cada linea de
chat, con `conversation_key` -- username del peer, o id de grupo, o `""`
para el log de sistema), `unread_counts`, `known_groups` (ver
[05-grupos.md](05-grupos.md)), `one_time_prekeys` (la mitad PRIVADA de las
OTPK propias, hasta que se consuman).

`migrateSchema()` es el mecanismo para cuando el formato de una tabla
cambia entre versiones del programa: usa `PRAGMA table_info(tabla)` para
detectar columnas viejas o que faltan, y aplica un `ALTER TABLE` no
destructivo cuando es posible (p.ej. añadir `created_at` conservando todo
el historial existente) o recrea la tabla vacia cuando el formato antiguo
no se puede traducir de forma limpia (con un comentario explicando por
que en cada caso). Se llama SIEMPRE antes de la primera consulta, tanto en
una base de datos recien creada como en una restaurada de disco.

## MainWindow: donde se junta todo

`client/src/MainWindow.hpp`/`.cpp` es el archivo mas grande del proyecto
porque es literalmente donde toda la orquestacion vive: recibe eventos de
`NetworkManager` (`onFrameReceived`, un `switch` sobre `MsgType` del mismo
estilo que `Router::handleFrame` en el servidor), decide que hacer con
`CryptoEngine` y `LocalStore`, y actualiza los widgets.

Un patron que se repite mucho y vale la pena reconocer: **separar la accion
real de la parte que abre un dialogo modal**. Por ejemplo,
`onAttachClicked()` solo abre el `QFileDialog` y, si el usuario elige un
archivo, llama a `startOutgoingFileTransfer(path)` -- que es donde esta TODA
la logica real. La razon es doble: (1) es mejor diseño, separa "pedir un
dato al usuario" de "que hacer con el dato", y (2) hace que se pueda probar
`startOutgoingFileTransfer` directamente en un test automatizado
(`QMetaObject::invokeMethod`) sin tener que lidiar con un dialogo modal de
verdad, que es notoriamente fragil de automatizar. El mismo patron se repite
con las invitaciones a grupo: en vez de un `QMessageBox` modal que
interrumpiria lo que estuvieras haciendo, la invitacion se queda en un panel
no-modal de la barra lateral (`inviteList_`) hasta que la mires.

Otro patron: **`ensureConversationListed(key, label)` es idempotente**. Se
llama desde muchos sitios distintos (recibir un mensaje, crear un grupo,
cargar el historial al loguear...) y siempre es seguro llamarla aunque esa
conversacion ya este listada -- comprueba `listedKeys_` y no hace nada si
ya estaba. Esto evita que cada sitio que "podria ser la primera vez que se
ve a este peer" tenga que llevar la cuenta el mismo de si ya la anadio o no.
