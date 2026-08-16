# 02 -- Criptografia: X3DH y Double Ratchet

Archivos: `crypto/include/templar/crypto/{Identity,X3DH,DoubleRatchet}.hpp`,
`crypto/src/{Identity,X3DH,DoubleRatchet}.cpp`. Todo aqui es criptografia
pura -- ni una sola linea sabe que existe una red o una base de datos.

Este es el modulo mas denso del proyecto. Vale la pena leerlo despacio;
tambien vale la pena leer los tests (`crypto/tests/test_x3dh.cpp`,
`crypto/tests/test_double_ratchet.cpp`) en paralelo, porque cada test
ejercita un caso concreto con nombres descriptivos.

## El problema en dos partes

Cifrado extremo a extremo necesita resolver dos problemas distintos:

1. **¿Como se ponen de acuerdo dos personas en una clave secreta compartida,
   sin que nadie que este escuchando la pueda calcular tambien?** -- eso es
   Diffie-Hellman, y X3DH es una variante pensada para chat asincrono
   (donde el otro puede estar desconectado).
2. **Una vez tienen esa clave compartida, ¿como cifran una conversacion
   entera de forma que comprometer una clave en un momento dado no
   comprometa TODOS los mensajes pasados y futuros?** -- eso es el Double
   Ratchet.

## Identity.hpp: los bloques basicos

Cada persona tiene una `Identity` (`Identity.hpp:36-54`): un par de firma
Ed25519 (`ed_`) y un par de intercambio X25519 (`x_`), generados una unica
vez al registrarse.

```cpp
X25519KeyPair generateX25519KeyPair();  // para Diffie-Hellman (crypto_box_keypair)
Ed25519KeyPair generateEd25519KeyPair(); // para firmar (crypto_sign_keypair)
```

Dos claves distintas para dos usos distintos, cada una con el algoritmo que
mejor encaja (X25519 para DH, Ed25519 para firmas) -- es la practica
estandar, no se puede usar la misma clave para ambas cosas de forma segura.

`diffieHellman()` (`Identity.cpp:44-51`) es la unica funcion de DH cruda de
todo el proyecto -- tanto X3DH como el Double Ratchet la reusan, para que
nunca puedan divergir en algo tan sensible. Nota que **lanza una excepcion**
si el resultado es "el elemento identidad" (un punto de orden bajo): eso
pasa si alguien te manda una clave publica maliciosa especificamente
construida para forzar un resultado predecible. libsodium ya lo detecta
internamente; aqui simplemente se deja propagar como error en vez de
seguir con una clave comprometida.

## X3DH: acordar una clave sin que el otro este conectado

Archivo: `X3DH.hpp`/`X3DH.cpp`. La idea central de X3DH ("Extended Triple
Diffie-Hellman") es: si Alice quiere escribirle a Bob por primera vez y Bob
esta desconectado, Alice necesita poder calcular una clave compartida
**ahora mismo**, usando solo material publico que Bob dejo preparado de
antemano en el servidor.

### Las prekeys de Bob

Cuando Bob se registra, genera (ademas de su `Identity`) una **signed
prekey**: un par X25519 mas una firma Ed25519 sobre su clave publica
(`CryptoEngine::CryptoEngine()`, `client/src/CryptoEngine.cpp:48-52`). Esa
firma es importante: demuestra que quien controla la identidad de Bob
genero de verdad esa prekey -- sin ella, el servidor (o alguien en medio)
podria sustituirla por una suya y montar un ataque de intermediario.
`PrekeyBundle::verify()` (`X3DH.cpp:35-38`) es exactamente esa comprobacion,
y **siempre** se llama antes de usar un bundle (`x3dhInitiate` la llama
internamente, `X3DH.cpp:41-43`).

Ademas de la signed prekey (reutilizable), Bob tambien publica un lote de
**one-time prekeys** (OTPK): claves X25519 de usar-y-tirar. El servidor
entrega como mucho una por bundle y la borra al entregarla
(`Database::takeOneTimePrekey`, `server/src/Database.cpp`) -- si esta
disponible, se usa para una cuarta DH que ni siquiera Bob puede repetir mas
adelante, dando una garantia extra de forward secrecy para ese primer
mensaje en concreto. Si el pool esta vacio (nadie lo ha reabastecido, o Bob
lleva mucho sin conectarse), X3DH sigue funcionando en modo degradado con
solo 3 DH -- sigue siendo seguro, solo pierde ese margen extra.

### Las cuatro DH

Esto es el nucleo de `x3dhInitiate` (`X3DH.cpp:40-63`). Alice tiene su
identidad (`IK_A`) y genera una clave efimera nueva solo para esta sesion
(`EK_A`). Bob ya publico su identidad (`IK_B`), su signed prekey (`SPK_B`) y
quiza una one-time prekey (`OPK_B`). Se calculan:

```
DH1 = DH(IK_A,  SPK_B)     <- ata la identidad de Alice a la prekey de Bob
DH2 = DH(EK_A,  IK_B)      <- ata la efimera de Alice a la identidad de Bob
DH3 = DH(EK_A,  SPK_B)     <- ata ambas efimeras/prekeys entre si
DH4 = DH(EK_A,  OPK_B)     <- (si hay OTPK) un extra de un solo uso
```

Cuatro combinaciones distintas de claves, no una sola DH. ¿Por que? Cada una
aporta una propiedad de seguridad diferente (DH1 autentica a Alice ante
quien tenga la privada de SPK_B, DH2 autentica a Bob ante quien tenga la
privada de EK_A...) -- combinadas, ni Alice ni Bob por separado pueden
"fingir" haber sido el otro, y comprometer una sola de las cuatro claves no
compromete el resultado. Los cuatro resultados se concatenan y se pasan por
HKDF-SHA256 (`deriveSharedSecret`, `X3DH.cpp:14-31`) para producir una unica
clave de 32 bytes (`SharedSecret`) -- esa es la que arranca el Double
Ratchet.

Bob calcula exactamente las mismas cuatro DH desde su lado
(`x3dhRespond`, `X3DH.cpp:65-83`) -- mismos numeros, orden de operandos
invertido porque cada quien usa su propia privada con la publica ajena, pero
el resultado de cada `diffieHellman()` es identico en ambos lados (esa es la
propiedad matematica que hace que Diffie-Hellman funcione).

### ¿Y si Bob esta desconectado?

Ese es justo el punto: Alice no necesita que Bob este online para calcular
la SK y cifrar el primer mensaje -- solo necesita el bundle publico que el
servidor ya tenia guardado. El mensaje cifrado se queda en la cola del
servidor (`mailbox`, ver [03-servidor.md](03-servidor.md)) hasta que Bob se
conecte y pueda completar `x3dhRespond`.

## Double Ratchet: cifrar la conversacion que sigue

Una vez hay una `SharedSecret`, arranca un `DoubleRatchet`
(`DoubleRatchet.hpp`/`DoubleRatchet.cpp`). La idea: en vez de cifrar todos
los mensajes con la misma clave (si esa clave se filtra, se pierde TODA la
conversacion), cada mensaje individual usa una clave distinta, derivada de
forma que sea imposible ir hacia atras (forward secrecy) y que un nuevo
intercambio DH cada cierto tiempo "cure" la conversacion aunque una clave
anterior se hubiera filtrado (post-compromise security).

### Dos tipos de cadena

Hay dos KDFs (funciones de derivacion de claves) distintas trabajando juntas:

**KDF de cadena simetrica** (`kdfChainKey`, `DoubleRatchet.cpp:37-46`): a
partir de una "chain key" actual, produce la SIGUIENTE chain key y una
"message key" de un solo uso, via HMAC-SHA256 con dos etiquetas fijas
distintas (`0x01` para avanzar la cadena, `0x02` para sacar la clave del
mensaje). Cada vez que cifras un mensaje, la cadena avanza un paso -- por
eso, aunque alguien capture la chain key actual, no puede reconstruir las
message keys de mensajes YA enviados (la funcion no es invertible).

**KDF de raiz** (`kdfRootKey`, `DoubleRatchet.cpp:16-32`): mezcla la "root
key" actual con el resultado de una NUEVA DH (via HKDF-SHA256) y produce la
siguiente root key mas la primera chain key de una cadena nueva. Esto pasa
cada vez que el otro lado manda un mensaje con una clave DH nueva (ver mas
abajo, "el paso de ratchet DH").

### Cifrar un mensaje: `encrypt()`

`DoubleRatchet.cpp:278-295`. Cada llamada:

1. Avanza la cadena de envio un paso (`kdfChainKey`), obteniendo una
   message key nueva.
2. Construye una cabecera (`RatchetHeader`): la clave publica DH actual del
   emisor, cuantos mensajes tenia la cadena anterior (`prevChainLen`), y el
   numero de este mensaje dentro de la cadena actual (`messageNumber`).
3. Cifra con XChaCha20-Poly1305 (`aeadEncrypt`, `DoubleRatchet.cpp:51-61`),
   usando la cabecera serializada como **datos asociados** (AD) -- eso
   significa que la cabecera viaja sin cifrar (el receptor la necesita para
   saber que hacer antes de poder descifrar nada), pero SI esta autenticada:
   si alguien la manipula un solo bit, la verificacion del AEAD falla.

Un detalle que vale la pena entender porque a primera vista parece
peligroso: el nonce del AEAD es **fijo, todo ceros**
(`DoubleRatchet.cpp:52`). Normalmente reusar un nonce con la misma clave es
catastrofico. Aqui es seguro porque cada `message key` se deriva una unica
vez y se usa para cifrar exactamente un mensaje -- la cadena nunca repite
una message key, asi que el PAR (clave, nonce) nunca se reutiliza, que es
la propiedad real que importa.

### Descifrar un mensaje: `decrypt()`

`DoubleRatchet.cpp:297-322`. Aqui esta la parte interesante: los mensajes
pueden llegar en cualquier orden (la red no garantiza orden dentro de una
misma sesion si hay reintentos, y desde luego no lo garantiza si Alice manda
tres mensajes seguidos y luego Bob responde a uno intermedio). El algoritmo:

1. **¿Es una clave de mensaje que ya me salte?** -- si la cabecera coincide
   con una entrada guardada en `skippedKeys_` (`tryPopSkippedKey`), se usa
   esa y se borra (ya no hace falta, y guardarla para siempre seria un fugazo
   de memoria y de seguridad).
2. **¿Trae una clave DH nueva del emisor?** -- si el `dhPub` de la cabecera
   no coincide con el que ya tenias guardado, es que el otro lado ha hecho
   su propio paso de ratchet. Antes de adoptar la clave nueva, se agotan
   (`skipMessageKeys`) las claves que faltaban de la cadena VIEJA -- por si
   alguno de esos mensajes llega mas tarde -- y luego se ejecuta
   `dhRatchetStep`: una DH nueva mezclada en la root key, produciendo tanto
   una cadena de recepcion nueva como (de cara al proximo mensaje propio)
   una cadena de envio nueva con una clave DH propia tambien nueva.
3. Se saltan las claves que falten hasta llegar al `messageNumber` de este
   mensaje concreto (por si llego fuera de orden dentro de la MISMA cadena).
4. Se deriva la message key de ese mensaje y se descifra.

Hay una cota (`kMaxSkip = 1000`, `DoubleRatchet.hpp:112`) para que un peer
malicioso no pueda declarar "este es el mensaje numero 4 mil millones" y
forzar que derives y guardes esa cantidad de claves en memoria -- eso
lanzaria una excepcion en vez de intentarlo.

### Quien arranca como que

Alice (quien inicio X3DH) llama `initAsSender` -- ya tiene una cadena de
envio lista desde el minuto uno (su propia efimera de X3DH, reaprovechada
como primer par de ratchet), pero NO tiene cadena de recepcion todavia (no
sabe la proxima clave DH de Bob hasta que el responda). Bob llama
`initAsReceiver` -- al reves: sin cadena de envio hasta que decida
responder, pero en cuanto descifra el primer mensaje de Alice, `decrypt()`
dispara automaticamente el primer `dhRatchetStep` y a partir de ahi ya tiene
ambas cadenas. Es asimetrico a proposito: asi es como Signal define el
protocolo.

### Datos asociados (AD) fijos por conversacion

Ademas de la cabecera (que cambia por mensaje), hay un AD fijo para toda la
conversacion: `IK_iniciador || IK_receptor`, en ese orden
(`buildAssociatedData`, `client/src/CryptoEngine.cpp:38-44`). Como el orden
es siempre "quien inicio X3DH primero", ambos lados calculan exactamente
los mismos bytes sin tener que coordinarse aparte. Esto ata cada mensaje
cifrado a ESTA pareja de identidades concreta -- si alguien intentara
reproducir el ciphertext en el contexto de otra conversacion, el AEAD lo
rechazaria.

### Persistencia

`serialize()`/`deserialize()` (`DoubleRatchet.cpp:324-392`) vuelcan TODO el
estado (ambas cadenas, contadores, claves saltadas pendientes) a un blob
binario plano. Ese blob contiene material criptografico sensible **en
claro** -- la propia clase lo dice en el comentario de cabecera -- por eso
quien lo usa (`LocalStore`, ver [04-cliente.md](04-cliente.md)) lo cifra
antes de tocar disco.

## Como experimentar con esto

Los tests son el mejor sitio para ver ejemplos concretos y aislados:

```bash
./build/crypto/templar_crypto_tests   # X3DH: bundle_verification, x3dh_agreement_with_otpk, ...
./build/crypto/templar_ratchet_tests  # Double Ratchet: out_of_order_delivery, tampered_ciphertext_rejected, ...
```

Cada nombre de test es literalmente el escenario que reproduce -- por
ejemplo `out_of_order_delivery` monta una conversacion, cifra varios
mensajes, los "entrega" a proposito en el orden incorrecto, y comprueba que
el receptor los descifra igual.
