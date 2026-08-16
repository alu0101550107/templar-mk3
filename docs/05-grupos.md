# 05 -- Grupos: cifrado 1 a 1 reciclado, sin cripto nueva

Archivos relevantes: `server/src/{Database,Router}.cpp` (las partes de
grupo), `client/src/MainWindow.cpp` (busca `sendGroupMessage`,
`continueGroupFanout`), `client/src/CreateGroupDialog.{hpp,cpp}`.

## La decision de diseño mas importante: fan-out, no cifrado de grupo

Signal "de verdad" para grupos usa **Sender Keys**: cada miembro genera una
clave simetrica propia, la reparte una vez por los canales 1 a 1 que ya
tiene con cada uno, y a partir de ahi cifra los mensajes de grupo UNA sola
vez con esa clave (todos los miembros pueden descifrarla). Es eficiente
para grupos grandes, pero es un subsistema criptografico nuevo entero.

Este proyecto usa la alternativa mas simple: **fan-out**. Cuando escribes
algo en un grupo, el cliente cifra el mensaje **una vez por cada miembro**,
usando el mismo Double Ratchet 1 a 1 que ya existiria (o se establece en
ese momento) si le escribieras directamente. El servidor recibe N envios
independientes y los reenvia igual que cualquier mensaje 1 a 1 -- no sabe
ni le hace falta saber que son "el mismo mensaje" repartido entre varios.

Para un grupo de amigos (no miles de miembros) esto es perfectamente
razonable: cero cripto nueva, se reutiliza el 100% del Double Ratchet ya
probado, y el coste (cifrar N veces en vez de una) es insignificante a esa
escala. La contrapartida es que no escala a grupos grandes -- si algun dia
hiciera falta, Sender Keys seria el siguiente paso logico.

## `sendGroupMessage`: el fan-out paso a paso

`client/src/MainWindow.cpp`. Cuando escribes en un grupo:

```cpp
void MainWindow::sendGroupMessage(const std::string& groupId, const std::string& text) {
  logChat(groupId, LineKind::Own, ...);          // se muestra ya, optimista
  activeGroupFanout_ = GroupFanout{groupId, text, /* lista de miembros menos yo */};
  continueGroupFanout();
}
```

`continueGroupFanout()` recorre la lista de miembros pendientes. Para cada
uno: si ya hay una sesion Double Ratchet establecida (`crypto_.hasSession`),
cifra y manda directo (`SendGroupMsg`, con `isFirst=0`). Si NO hay sesion
todavia (es la primera vez que le escribes a esa persona, sea por el grupo
o porque nunca hablaste con ella 1 a 1), hace falta el mismo baile de X3DH
que en un chat normal: pide `FetchPrekeyBundle`, **para el bucle ahi
mismo**, y lo retoma desde el `case MsgType::PrekeyBundle` de
`onFrameReceived` en cuanto llega la respuesta.

Este "parar y retomar" es necesario porque el protocolo (`FetchPrekeyBundle`
-> `PrekeyBundle`) no dice a quien pertenece la respuesta -- solo puede
haber una peticion de bootstrap en vuelo a la vez por diseño, asi que el
resto de la cola de miembros simplemente espera su turno.

Importante: las sesiones Double Ratchet estan indexadas solo por username
(`CryptoEngine::sessions_`), no por "conversacion". Si ya le escribiste 1 a
1 a Bob antes, el mensaje de grupo hacia Bob usa esa MISMA sesion (avanza el
ratchet un paso mas) -- no crea una sesion "de grupo" aparte.

## Autorizacion: el servidor decide, no el cliente

Aunque el servidor no ve el contenido, si necesita decidir **quien puede
mandar que a quien** -- si eso se dejara enteramente al cliente, cualquiera
con un cliente modificado podria auto-invitarse a un grupo o mandar
mensajes a nombre de un grupo del que no es miembro.

`Router::handleSendGroupMsg` comprueba, antes de reenviar nada, que tanto
quien manda como el destinatario sean miembros ACTUALES del grupo
(`db_.isGroupMember(groupId, ...)`, dos veces). `handleInviteToGroup`
comprueba que quien invita sea el admin. `handleKickFromGroup` igual. La
tabla `group_members` en la base de datos del servidor es la autoridad --
el cliente mantiene su propia copia en memoria (`groups_` en `MainWindow`)
solo para pintar la interfaz, pero cada operacion sensible se revalida en
el servidor.

## El ciclo de vida de un grupo

1. **Crear** (`CreateGroup` -> `GroupCreated`): el servidor genera un id
   opaco aleatorio (16 bytes de libsodium en hex -- no adivinable, no
   secuencial) y te añade como admin y unico miembro.
2. **Invitar** (`InviteToGroup`): el admin manda un username. El servidor
   valida que exista y no sea ya miembro, y encola una invitacion
   (`group_invites`, la misma logica de cola-hasta-que-se-conecte que ya
   usa `mailbox` para mensajes -- ver [03-servidor.md](03-servidor.md)).
3. **Aceptar/rechazar explicitamente** (`AcceptGroupInvite`/`RejectGroupInvite`):
   a proposito no hay incorporacion automatica ni silenciosa. La invitacion
   se muestra en un panel no modal de la barra lateral (`inviteList_`, ver
   [04-cliente.md](04-cliente.md)) hasta que decides que hacer con ella.
4. **Expulsar** (`KickFromGroup`, solo admin) / **salir** (`LeaveGroup`,
   cualquiera, incluido el admin -- si el admin se va y quedan mas
   miembros, el servidor promueve automaticamente al mas antiguo).
5. **Snapshot al loguear** (`MyGroups`): justo despues de `LoginOk`, el
   servidor manda la lista COMPLETA de tus grupos actuales con nombre,
   admin y miembros. El cliente nunca "recuerda" membresia entre sesiones
   por su cuenta -- siempre se refresca desde el servidor, que es la
   autoridad.

## El bug del "grupo fantasma" y por que `known_groups` existe

Un caso real que se encontro despues de implementar todo lo anterior: si
salias de un grupo (o te expulsaban, o el grupo se borraba) y luego
reconectabas, el grupo **reaparecia** en la barra lateral -- con su id en
crudo como nombre, y con un punto de presencia como si fuera un chat
normal.

La causa: el historial de mensajes de un grupo se guarda en `LocalStore`
reusando las mismas tablas `history`/`unread_counts` que un chat 1 a 1 (con
el id de grupo como `conversation_key` -- ver
[04-cliente.md](04-cliente.md)). Al reconectar, `loadHistoryFromStore()`
recorre TODAS las claves con historial local y las relista en la barra
lateral -- y en el momento exacto en que eso corre, el snapshot `MyGroups`
(que es quien sabe que ya no eres miembro) **todavia no habia llegado**
(llega en un frame aparte, justo despues). Sin ese snapshot, el codigo no
tenia forma de distinguir "esto es un grupo viejo" de "esto es un peer
normal".

El arreglo fue añadir una tabla minima, `known_groups`, que solo recuerda
"esta clave fue un grupo alguna vez" -- independiente de si sigues siendo
miembro (`LocalStore::rememberGroupKey`/`loadKnownGroupKeys`). Con eso,
`loadHistoryFromStore()` puede saltarse cualquier clave marcada como grupo
sin necesitar el snapshot todavia: el historial se sigue cargando (por si
hace falta), pero la clave no se vuelve a listar en la barra lateral a
ciegas -- eso queda en manos de `GroupCreated`/`MyGroups`, que ya saben con
certeza si sigues dentro o no.

Es un buen ejemplo de un tipo de bug que solo aparece por la INTERACCION
entre dos piezas que, cada una por separado, funcionaban bien: la reutilizacion
del historial (razonable, ahorra una tabla nueva) y el orden de llegada de
los frames al loguear (tambien razonable). El test de regresion
(`client/tests/group_smoke.cpp`, la seccion final) reproduce exactamente
este escenario: sale del grupo, reconecta con una instancia NUEVA de
`MainWindow` (simulando cerrar y reabrir la app), y comprueba que el grupo
no reaparece.
