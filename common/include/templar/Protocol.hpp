#pragma once

#include <cstdint>
#include <optional>

#include "templar/Wire.hpp"

namespace templar::proto {

enum class MsgType : uint8_t {
  Register = 1,
  RegisterOk = 2,
  RegisterErr = 3,
  Login = 4,
  LoginOk = 5,
  LoginErr = 6,
  PublishOtpk = 7,
  FetchPrekeyBundle = 8,
  PrekeyBundle = 9,
  PrekeyBundleErr = 10,
  // Cliente -> servidor: str recipientUsername, u8 isFirst, blob
  // senderIdentityPkX25519, blob senderEphemeralPk, blob ciphertext, blob
  // usedOneTimePrekeyPub. Los ultimos cuatro campos son vacios si !isFirst
  // (no hace falta repetir el bootstrap X3DH en cada mensaje de una sesion
  // ya establecida).
  SendMsg = 11,
  // Servidor -> cliente: u32 mailboxId, str sender, u8 isFirst, blob
  // senderIdentityPkX25519, blob senderEphemeralPk, blob ciphertext, blob
  // usedOneTimePrekeyPub, u64 sentAt (epoch, segundos -- cuando el servidor
  // recibio el SendMsg original, no cuando este DeliverMsg llega al
  // destinatario: para un mensaje en cola mientras el destinatario estaba
  // desconectado, esos dos momentos pueden diferir en horas o dias). Se
  // manda igual en entrega en vivo (justo tras SendMsg) que al vaciar el
  // buzon en el siguiente login (Router::flushMailbox) -- mismo formato en
  // ambos casos, el cliente no distingue de donde vino.
  DeliverMsg = 12,
  Ack = 13,
  // Cliente -> servidor: "avisame si el usuario X se conecta o desconecta".
  // Payload: str username. El servidor responde de inmediato con un
  // PresenceUpdate del estado actual, y de nuevo cada vez que cambie
  // mientras la sesion siga conectada (suscripcion solo en memoria, no se
  // persiste en ningun sitio).
  SubscribePresence = 14,
  // Servidor -> cliente: str username, u8 online (1/0).
  PresenceUpdate = 15,

  // --- Grupos ---
  // El cifrado sigue siendo puramente 1-a-1 (Double Ratchet por pareja): un
  // mensaje de grupo se manda como N envios independientes, uno cifrado para
  // cada miembro con el canal que ya existe (o se bootstrea) con esa persona.
  // El servidor solo enruta y autoriza (solo el admin invita/expulsa, solo
  // los miembros mandan/reciben); nunca ve texto plano.

  // Cliente -> servidor: str name. El servidor genera un id opaco aleatorio,
  // crea el grupo con el emisor como admin y unico miembro inicial.
  CreateGroup = 16,
  // Servidor -> cliente (respuesta a CreateGroup): str groupId, str name.
  GroupCreated = 17,
  // Cliente -> servidor (solo admin): str groupId, str inviteeUsername.
  InviteToGroup = 18,
  // Servidor -> cliente (al invitado; en vivo si esta online, o al hacer
  // login si no lo estaba -- misma logica de cola que el mailbox de DMs):
  // u32 inviteId, str groupId, str groupName, str inviterUsername.
  GroupInvite = 19,
  // Cliente -> servidor: u32 inviteId. El groupId real lo determina el
  // servidor a partir del inviteId (ownership-checked contra la sesion),
  // no hace falta que el cliente lo repita.
  AcceptGroupInvite = 20,
  RejectGroupInvite = 21,
  // Servidor -> cliente (a los miembros ya existentes que esten online, para
  // que refresquen su UI): str groupId, str username.
  GroupMemberJoined = 22,
  // Cliente -> servidor (solo admin): str groupId, str username.
  KickFromGroup = 23,
  // Servidor -> cliente (a los miembros restantes online Y al expulsado, que
  // debe quitar el grupo de su barra lateral): str groupId, str username.
  GroupMemberKicked = 24,
  // Cliente -> servidor: str groupId, str recipientUsername, u8 isFirst,
  // blob senderIdentityPkX25519, blob senderEphemeralPk, blob ciphertext.
  // El cliente manda una de estas por cada miembro del grupo (fan-out).
  SendGroupMsg = 25,
  // Servidor -> cliente: u32 mailboxId, str groupId, str sender, u8 isFirst,
  // blob senderIdentityPkX25519, blob senderEphemeralPk, blob ciphertext,
  // blob usedOneTimePrekeyPub, u64 sentAt (mismo significado que en
  // DeliverMsg -- ver su comentario).
  // Se responde con el mismo Ack{mailboxId} que ya existe para DeliverMsg.
  DeliverGroupMsg = 26,
  // Servidor -> cliente, una vez justo tras LoginOk: snapshot completo de
  // los grupos del usuario. u32 groupCount, y por cada uno: str groupId,
  // str name, str adminUsername, u32 memberCount, memberCount * str.
  MyGroups = 27,
  // Servidor -> cliente: fallo generico en una operacion de grupo (crear,
  // invitar, expulsar). str context (p.ej. "invite"), str message.
  GroupErr = 28,
  // Cliente -> servidor: str groupId. Cualquier miembro puede salir,
  // incluido el admin -- si el admin sale y quedan mas miembros, se
  // promueve automaticamente al mas antiguo de los que quedan.
  LeaveGroup = 29,
  // Servidor -> cliente (a los miembros que quedan, online): str groupId,
  // str username (quien se fue), str currentAdminUsername (el admin
  // vigente tras la salida, cambie o no). El que se va NO recibe esta
  // notificacion de vuelta -- actualiza su propia UI de inmediato al
  // mandar LeaveGroup, sin esperar confirmacion.
  GroupMemberLeft = 30,

  // --- Archivos como blob cifrado en el servidor ---
  // Sustituye al envio de archivos trocito-a-trocito por el canal Double
  // Ratchet de cada destinatario (demasiado caro para grupos -- repetiria
  // el archivo entero por miembro -- e inviable con el destinatario
  // desconectado -- miles de filas de buzon por un solo archivo). Ahora: el
  // archivo se cifra UNA vez con una clave simetrica aleatoria propia (ver
  // templar::crypto::FileCrypto, crypto_secretstream_xchacha20poly1305 --
  // nada que ver con ninguna sesion Double Ratchet), se sube UNA vez al
  // servidor como blob opaco, y lo unico que viaja por el canal cifrado de
  // cada destinatario (mensaje FileBlobPointer, ver MessagePayload.hpp) es
  // un puntero pequeno: el id del blob mas la clave para abrirlo. El
  // servidor guarda bytes que no puede descifrar -- ni gana ni pierde
  // privacidad respecto al mecanismo anterior, solo pasa de enrutador puro
  // a tambien guardar (temporalmente) datos opacos.

  // Cliente -> servidor: u64 totalSize, str filename (solo para mostrar en
  // el error si se rechaza; el nombre real viaja dentro de
  // FileBlobPointer, cifrado). El servidor comprueba aqui el limite de
  // tamano de verdad (antes solo lo aplicaba el cliente, por cortesia).
  UploadBlobBegin = 31,
  // Servidor -> cliente: str blobId (id opaco aleatorio, asignado aqui).
  UploadBlobBeginOk = 32,
  // Servidor -> cliente: str reason.
  UploadBlobBeginErr = 33,
  // Cliente -> servidor, repetido una vez por fragmento: str blobId, blob
  // chunkCifrado (ya cifrado con FileCrypto antes de salir del cliente).
  UploadBlobChunk = 34,
  // Cliente -> servidor, una vez al terminar de mandar todos los
  // fragmentos: str blobId.
  UploadBlobEnd = 35,
  UploadBlobEndOk = 36,
  UploadBlobEndErr = 37,

  // Cliente -> servidor: str blobId.
  DownloadBlob = 38,
  // Servidor -> cliente, repetido una vez por fragmento: str blobId, blob
  // chunkCifrado.
  BlobData = 39,
  // Servidor -> cliente, al terminar de mandar todos los fragmentos: str
  // blobId.
  BlobEnd = 40,
  // Servidor -> cliente: str blobId, str reason (no existe, caduco, o
  // nunca se completo la subida).
  BlobNotFound = 41,
};

// Cota de seguridad: ningún mensaje legítimo del protocolo debería acercarse
// a esto. Protege contra un peer que anuncie una longitud absurda para forzar
// una asignación de memoria gigante (DoS).
constexpr uint32_t kMaxFrameSize = 16u * 1024 * 1024;

struct Frame {
  MsgType type;
  Bytes payload;
};

// Empaqueta un frame completo: [4 bytes BE longitud = 1(tipo) + payload][1 byte tipo][payload]
Bytes encodeFrame(MsgType type, const Bytes& payload);

// Acumula bytes crudos de un stream TCP y va extrayendo frames completos.
// Necesario porque TCP no respeta límites de mensaje: un frame puede llegar
// partido en varias lecturas, o varios frames pueden llegar juntos en una sola.
// Se usa igual en el servidor (Boost.Asio) y en el cliente (QTcpSocket).
class FrameParser {
 public:
  void feed(const uint8_t* data, size_t len);
  void feed(const Bytes& data) { feed(data.data(), data.size()); }

  // Devuelve el siguiente frame completo si ya está disponible, o
  // std::nullopt si aún faltan bytes por llegar. Puede lanzar
  // std::runtime_error si la longitud declarada es inválida.
  std::optional<Frame> next();

 private:
  Bytes buf_;
};

}  // namespace templar::proto
