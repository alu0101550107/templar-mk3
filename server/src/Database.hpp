#pragma once

#include <sqlite3.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "templar/Wire.hpp"

namespace templar::server {

using templar::proto::Bytes;

// Wrapper sobre SQLite. El servidor solo guarda material PÚBLICO (claves
// públicas, prekeys firmadas, blobs cifrados de mensajes) y el hash de
// contraseña -- nunca claves privadas ni texto plano. Todos los accesos se
// serializan con un mutex propio: para el volumen de un chat de grupo
// pequeño es más que suficiente y evita la complejidad de un hilo dedicado
// a DB (que sí valdría la pena si esto creciera mucho).
class Database {
 public:
  explicit Database(const std::string& path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  struct UserRecord {
    int64_t id;
    std::string username;
    std::string passwordHash;
    Bytes identityPkX25519;
    Bytes identityPkEd25519;
    Bytes signedPrekeyPub;
    Bytes signedPrekeySig;
  };

  struct MailboxRow {
    int64_t id;
    std::string senderUsername;
    bool isFirst;
    Bytes senderIdentityPkX25519;  // vacío si !isFirst
    Bytes senderEphemeralPk;       // vacío si !isFirst
    Bytes ciphertext;
    std::string groupId;         // vacío si es un mensaje 1-a-1 normal
    Bytes usedOneTimePrekeyPub;  // vacío si !isFirst o no habia ninguna disponible
    // Epoch (segundos) de cuando el SendMsg original llego al servidor --
    // NO de cuando se entrega (que puede ser mucho despues, si el
    // destinatario estaba desconectado). Se relaya tal cual en DeliverMsg/
    // DeliverGroupMsg para que el cliente pueda mostrar la hora real de
    // envio en vez de la hora de login.
    int64_t createdAt = 0;
  };

  struct GroupRecord {
    std::string id;
    std::string name;
    std::string adminUsername;
  };

  struct GroupWithMembers {
    GroupRecord group;
    std::vector<std::string> members;
  };

  struct GroupInviteRow {
    int64_t id;
    std::string groupId;
    std::string groupName;
    std::string inviterUsername;
  };

  // Solo metadatos -- los bytes cifrados en si viven en disco, no aqui (ver
  // BlobStore). No se guarda el nombre real del archivo: viaja dentro del
  // mensaje FileBlobPointer, cifrado extremo a extremo, el servidor no
  // necesita saberlo para nada de lo que hace con el blob.
  struct BlobRecord {
    std::string id;
    std::string ownerUsername;
    uint64_t size;
    int64_t createdAt;
    bool complete;
  };

  // Devuelve false si el username ya está en uso.
  bool createUser(const std::string& username, const std::string& passwordHash,
                  const Bytes& identityPkX25519, const Bytes& identityPkEd25519,
                  const Bytes& signedPrekeyPub, const Bytes& signedPrekeySig);

  std::optional<UserRecord> findUser(const std::string& username);

  // sentAt: epoch (segundos) que se guarda como `created_at` y luego se
  // relaya en DeliverMsg/DeliverGroupMsg -- lo decide el LLAMADOR (no esta
  // funcion) para que sea el mismo valor exacto usado en la entrega en vivo
  // (ver Router::relayEncryptedMessage), en vez de leerlo dos veces del
  // reloj con el riesgo de que difieran por unos milisegundos.
  int64_t enqueueMessage(const std::string& recipientUsername, const std::string& senderUsername,
                         bool isFirst, const Bytes& senderIdentityPkX25519,
                         const Bytes& senderEphemeralPk, const Bytes& ciphertext, int64_t sentAt,
                         const std::string& groupId = "",
                         const Bytes& usedOneTimePrekeyPub = {});

  std::vector<MailboxRow> fetchPending(const std::string& username);

  // Solo borra la fila si pertenece realmente a `username` (evita que un
  // usuario borre mensajes ajenos falsificando el id).
  void ackMessage(int64_t mailboxId, const std::string& username);

  // --- Grupos ---

  // Genera un id opaco aleatorio (no adivinable), crea el grupo con
  // `adminUsername` como admin y unico miembro inicial. Devuelve el id.
  std::string createGroup(const std::string& name, const std::string& adminUsername);

  std::optional<GroupRecord> findGroup(const std::string& groupId);

  bool isGroupMember(const std::string& groupId, const std::string& username);

  void addGroupMember(const std::string& groupId, const std::string& username);

  // No falla si `username` no era miembro (idempotente).
  void removeGroupMember(const std::string& groupId, const std::string& username);

  std::vector<std::string> listGroupMembers(const std::string& groupId);

  // Cambia el admin de un grupo existente (p.ej. al salir el admin y
  // promover al miembro mas antiguo que queda).
  void setGroupAdmin(const std::string& groupId, const std::string& newAdmin);

  // Borra el grupo entero (fila propia, miembros e invitaciones pendientes
  // asociadas) -- se usa cuando el ultimo miembro sale.
  void deleteGroup(const std::string& groupId);

  // Todos los grupos de los que `username` es miembro, cada uno con su
  // lista de miembros ya cargada -- para el snapshot MyGroups al loguear.
  std::vector<GroupWithMembers> listGroupsForUser(const std::string& username);

  int64_t enqueueGroupInvite(const std::string& groupId, const std::string& inviteeUsername,
                             const std::string& inviterUsername);

  std::vector<GroupInviteRow> fetchPendingInvites(const std::string& username);

  // Borra la invitacion solo si `inviteId` pertenece de verdad a `username`
  // (mismo criterio de propiedad que ackMessage). Devuelve el groupId si la
  // borro, nullopt si no existia o no era suya.
  std::optional<std::string> resolveInvite(int64_t inviteId, const std::string& username);

  // --- One-time prekeys (X3DH) ---
  // Solo se guarda la parte publica -- la privada nunca sale del cliente
  // que la genero. Cada fila se entrega como mucho una vez (takeOneTimePrekey
  // la borra al leerla) para que la propiedad de "un solo uso" de X3DH sea
  // real tambien desde el punto de vista del servidor.
  void addOneTimePrekeys(const std::string& username, const std::vector<Bytes>& publicKeys);
  std::optional<Bytes> takeOneTimePrekey(const std::string& username);

  // --- Blobs de archivo (ver BlobStore.hpp) ---

  // Genera un id opaco nuevo, inserta la fila (complete=0) y lo devuelve.
  std::string createBlobRecord(const std::string& ownerUsername, uint64_t declaredSize);
  void markBlobComplete(const std::string& blobId);
  std::optional<BlobRecord> findBlob(const std::string& blobId);
  // Ids de blobs creados antes de `epochSeconds` -- para el barrido
  // periodico de limpieza (ver el temporizador en main.cpp).
  std::vector<std::string> listBlobsCreatedBefore(int64_t epochSeconds);
  void deleteBlobRecord(const std::string& blobId);
  // Ids de TODOS los blobs de un dueno, completos o no -- usado por
  // BlobStore::beginUpload para calcular su ocupacion real en disco antes
  // de aplicar la cuota por usuario.
  std::vector<std::string> listBlobIdsForOwner(const std::string& ownerUsername);

 private:
  void execOrThrow(const char* sql);
  void migrateMailboxSchema();

  // Topes de filas pendientes por destinatario -- generosos para
  // acumulacion offline real (alguien desconectado dias), acotados para
  // que una cuenta (legitima o comprometida) no pueda llenar sin limite el
  // buzon/las invitaciones de otra persona. Ver enqueueMessage/
  // enqueueGroupInvite.
  static constexpr int64_t kMaxPendingMailboxPerRecipient = 300;
  static constexpr int64_t kMaxPendingInvitesPerRecipient = 50;
  // Tope total (no por llamada -- eso ya lo limita handlePublishOtpk en
  // Router) de one-time prekeys guardadas por usuario, para que nadie
  // pueda llenar el almacenamiento del servidor llamando PublishOtpk en
  // bucle sin fin. Ver addOneTimePrekeys.
  static constexpr int64_t kMaxOneTimePrekeysPerUser = 500;

  sqlite3* db_ = nullptr;
  std::mutex mutex_;
};

}  // namespace templar::server
