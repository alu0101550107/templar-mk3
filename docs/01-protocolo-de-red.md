# 01 -- Protocolo de red

Archivos: `common/include/templar/Wire.hpp`, `common/include/templar/Protocol.hpp`,
`common/src/Protocol.cpp`.

## Por que hace falta un framing propio

TCP es un flujo de bytes, no de mensajes: cuando escribes 200 bytes en un
socket, el otro lado puede recibirlos en un solo `read()`, o en tres
`read()` de 80+80+40, o pegados con el mensaje siguiente. Si no marcas donde
empieza y termina cada mensaje, no hay forma de saber donde cortar.

La solucion de este proyecto (y la mas comun) es **prefijar cada mensaje con
su longitud**. Es lo que hace `encodeFrame`:

```
[4 bytes big-endian: longitud][1 byte: tipo de mensaje][payload...]
```

`common/src/Protocol.cpp:7-19`. El campo de longitud cubre "tipo + payload"
(por eso `innerLen = payload.size() + 1`), asi que con leer 4 bytes ya sabes
exactamente cuantos bytes mas hacen falta para tener el frame completo.

## FrameParser: reensamblar el flujo

`FrameParser` (`common/src/Protocol.cpp:21-44`) es el otro lado de la
moneda. Se le van dando trozos de bytes segun van llegando del socket
(`feed()`), y el (`next()`) devuelve un frame completo en cuanto haya
suficientes bytes acumulados, o `std::nullopt` si aun falta.

```cpp
void FrameParser::feed(const uint8_t* data, size_t len) {
  buf_.insert(buf_.end(), data, data + len);   // se acumula, sin mas
}

std::optional<Frame> FrameParser::next() {
  if (buf_.size() < 4) return std::nullopt;          // ni la longitud ha llegado entera
  uint32_t innerLen = /* leer los 4 bytes big-endian */;
  if (buf_.size() < 4 + innerLen) return std::nullopt;  // longitud si, cuerpo no
  // ... construir el Frame y descartar esos bytes del buffer ...
}
```

Fijate que `next()` puede llamarse en bucle tras un solo `feed()`: si llegaron
tres mensajes pegados en un mismo paquete TCP, las tres llamadas devuelven
cada una su frame. Esto es exactamente lo que hacen tanto el servidor
(`Session::run`, ver [03-servidor.md](03-servidor.md)) como el cliente
(`NetworkManager::onReadyRead`, ver [04-cliente.md](04-cliente.md)):

```cpp
parser_.feed(datos_que_acaban_de_llegar);
while (auto frame = parser_.next()) {
  // procesar frame->type, frame->payload
}
```

Tambien hay una cota de seguridad: `kMaxFrameSize = 16 MiB`
(`Protocol.hpp:37`). Sin ella, un peer malicioso podria mandar una longitud
absurda (4 GB) y forzar una asignacion de memoria gigante antes de que se
sepa que el mensaje es invalido -- un DoS trivial. Con la cota, ese frame se
rechaza de inmediato lanzando una excepcion.

## Writer / Reader: el contenido de cada payload

Una vez tienes el payload (los bytes despues del tipo), hace falta
serializar/deserializar los campos concretos de cada mensaje (un string, un
numero, una clave publica...). Eso es `Writer`/`Reader` en `Wire.hpp`.

Todo es big-endian y con longitud explicita donde hace falta:

- `u8`/`u32`/`u64`: enteros de ancho fijo.
- `blob(data, len)`: un `u32` con la longitud seguido de esos bytes. Sirve
  para cualquier cosa binaria -- claves publicas, firmas, ciphertexts.
- `str(s)`: exactamente lo mismo que `blob`, pero para `std::string`.

Ejemplo real, construir un `Login` (`client/src/MainWindow.cpp`, dentro de
`onLoginClicked`):

```cpp
Writer w;
w.str(username);
w.str(password);
net_.sendFrame(MsgType::Login, w.take());
```

Y leerlo en el servidor (`server/src/Router.cpp`, `handleLogin`):

```cpp
Reader r(payload);
std::string username = r.str();
std::string password = r.str();
```

`Reader` lanza `std::runtime_error` si le piden mas bytes de los que quedan
(`need()`, `Wire.hpp:92-96`) -- asi que un payload corrupto o forjado
provoca una excepcion controlada en vez de leer memoria fuera de rango.
Todo el codigo que parsea frames esta dentro de un `try/catch` (ver
`Router::handleFrame` y `MainWindow::onFrameReceived`) precisamente por
esto: un mensaje malformado tira ESE mensaje, no la conexion entera ni el
proceso.

## Los tipos de mensaje (`MsgType`)

Todo vive en un unico `enum class MsgType : uint8_t` en `Protocol.hpp`.
Agrupados por tema (los numeros exactos estan en el propio archivo, con
comentarios en cada uno explicando su payload):

- **Cuenta**: `Register`/`RegisterOk`/`RegisterErr`, `Login`/`LoginOk`/`LoginErr`.
- **Arranque de conversacion (X3DH)**: `FetchPrekeyBundle` -> `PrekeyBundle`/`PrekeyBundleErr`,
  `PublishOtpk` (subir prekeys de un solo uso -- ver [02-criptografia.md](02-criptografia.md)).
- **Mensajeria 1 a 1**: `SendMsg` (cliente -> servidor) / `DeliverMsg`
  (servidor -> cliente) / `Ack` (confirmar que se proceso, para poder
  borrarlo de la cola de pendientes).
- **Presencia**: `SubscribePresence` / `PresenceUpdate`.
- **Grupos**: `CreateGroup`, `InviteToGroup`, `AcceptGroupInvite`/`RejectGroupInvite`,
  `KickFromGroup`, `LeaveGroup`, `GroupMemberJoined`/`GroupMemberKicked`/`GroupMemberLeft`,
  `SendGroupMsg`/`DeliverGroupMsg`, `MyGroups` (snapshot al loguear), `GroupErr`.
  Ver [05-grupos.md](05-grupos.md) para el porque de cada uno.

Cada tipo tiene un comentario en `Protocol.hpp` documentando su payload
exacto (que campos, en que orden) -- esa es la "especificacion" del
protocolo, no hay un documento aparte: el enum y sus comentarios SON el
contrato entre cliente y servidor.

## Como se prueba esto en aislamiento

`FrameParser`/`Writer`/`Reader` no dependen de sockets reales -- se pueden
instanciar y probar con bytes en memoria. En este proyecto no hay un test
unitario dedicado solo a ellos (se ejercitan indirectamente en todos los
tests de integracion, que si mandan bytes reales por un socket TCP/TLS de
verdad), pero si quieres experimentar, es la forma mas rapida de ver el
framing en accion sin levantar nada:

```cpp
Writer w;
w.str("hola");
w.u32(42);
Bytes frame = encodeFrame(MsgType::Login, w.take());

FrameParser p;
p.feed(frame);
auto f = p.next();  // f->type == MsgType::Login
Reader r(f->payload);
std::cout << r.str() << " " << r.u32();  // "hola 42"
```
