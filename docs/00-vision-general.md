# 00 -- Vision general

Esta guia (y las que siguen en esta carpeta) asume que ya sabes programar en
C++ pero no necesariamente que conozcas Qt, Boost.Asio, o como funciona un
protocolo criptografico tipo Signal por dentro. La idea es que puedas leer
esto y luego ir al codigo real con contexto suficiente para entenderlo.

## El problema que resuelve

Quieres un chat entre varias personas, pero no quieres que el servidor (que
tu u otra persona administra) pueda leer las conversaciones. El servidor
tiene que poder **enrutar** mensajes sin poder **leerlos**.

La solucion clasica (la misma que usa Signal, WhatsApp, etc.) es cifrado
extremo a extremo: cada par de personas que hablan establece una clave
compartida que solo ellas dos conocen, calculada mediante intercambios
Diffie-Hellman. El servidor ve bytes cifrados y ya esta.

## Los cuatro modulos

```
crypto/   <- no sabe nada de red. Solo funciones puras: "dame estos bytes cifrados".
common/   <- el "idioma" que hablan cliente y servidor por la red (framing binario).
server/   <- Boost.Asio + SQLite. Reenvia bytes opacos, guarda cuentas y colas de mensajes.
client/   <- Qt6. Interfaz, y la orquestacion: cuando cifrar, cuando descifrar, que guardar.
```

Esta separacion es deliberada: `crypto/` se puede testear sin arrancar
ningun servidor ni abrir ninguna ventana (ver `crypto/tests/`), y el
servidor jamas necesita saber que es X3DH o un Double Ratchet -- para el,
todo mensaje es un blob de bytes que reenvia.

## El recorrido de un mensaje (vista de pajaro)

Cuando Alice le escribe "hola" a Bob por primera vez:

1. **Cliente de Alice**: no tiene todavia una "sesion" cifrada con Bob, asi
   que primero le pide al servidor el *prekey bundle* publico de Bob
   (`FetchPrekeyBundle` -> `PrekeyBundle`, ver
   [01-protocolo-de-red.md](01-protocolo-de-red.md)).
2. Con ese bundle, el cliente de Alice ejecuta **X3DH**
   (`crypto::x3dhInitiate`, ver [02-criptografia.md](02-criptografia.md)) y
   obtiene una clave compartida de 32 bytes que nadie mas puede calcular.
3. Esa clave compartida arranca un **Double Ratchet**: Alice cifra "hola"
   con `DoubleRatchet::encrypt`, produciendo un `RatchetMessage`.
4. El cliente manda ese ciphertext al servidor dentro de un frame `SendMsg`.
5. El servidor (`Router::handleSendMsg`, ver
   [03-servidor.md](03-servidor.md)) NO lo descifra -- solo lo guarda en una
   cola (`mailbox`) y, si Bob esta conectado ahora mismo, se lo reenvia tal
   cual dentro de un frame `DeliverMsg`.
6. El cliente de Bob recibe el frame, completa su lado de X3DH
   (`x3dhRespond`) y descifra con `DoubleRatchet::decrypt`.
7. Ambos guardan la sesion ya establecida (identidad + estado del ratchet)
   cifrada en disco (`LocalStore`, ver [04-cliente.md](04-cliente.md)), asi
   que el siguiente mensaje no repite el paso 1-2: usa directamente
   `encryptNext`/`decryptNext`.

Ese es el 90% del proyecto. Todo lo demas (grupos, archivos, presencia,
busqueda) son variaciones sobre este mismo patron: cifra en el cliente,
reenvia opaco en el servidor, descifra en el otro cliente.

## Por donde seguir

- [01-protocolo-de-red.md](01-protocolo-de-red.md) -- como se empaquetan los
  mensajes para viajar por TCP, y que tipos de mensaje existen.
- [02-criptografia.md](02-criptografia.md) -- X3DH y Double Ratchet paso a
  paso, con las formulas reales del codigo.
- [03-servidor.md](03-servidor.md) -- Boost.Asio con corutinas, el modelo de
  concurrencia, la base de datos.
- [04-cliente.md](04-cliente.md) -- Qt (señales/slots), la estructura de
  `MainWindow`, `LocalStore`.
- [05-grupos.md](05-grupos.md) -- como se construyen los grupos por encima
  de canales 1 a 1 sin cifrado nuevo.
- [06-tests.md](06-tests.md) -- que prueba cada test y por que estan
  escritos asi.
