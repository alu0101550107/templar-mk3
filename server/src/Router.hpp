#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "BlobStore.hpp"
#include "Database.hpp"
#include "templar/Protocol.hpp"

namespace templar::server {

class Session;

// Lógica de aplicación: registro/login de usuarios, distribución de prekey
// bundles y enrutamiento de mensajes cifrados. El Router nunca ve texto
// plano -- todo lo que pasa por SendMsg/DeliverMsg es opaco para él.
class Router {
 public:
  Router(Database& db, BlobStore& blobStore);

  void handleFrame(const std::shared_ptr<Session>& session, templar::proto::MsgType type,
                   const templar::proto::Bytes& payload);

  void onDisconnect(const std::shared_ptr<Session>& session);

  // --- Limite de conexiones concurrentes por IP ---
  // Llamado desde listener() en main.cpp, ANTES de intentar el handshake
  // TLS (para rechazar barato) -- no forma parte de la logica de aplicacion
  // del resto de la clase, pero vive aqui porque ya es donde vive el estado
  // por IP analogo (los intentos de login/registro). Independiente de si la
  // conexion llega a loguearse o no.
  static constexpr int kMaxConnectionsPerIp = 20;

  bool tryRegisterConnection(const std::string& ip);
  void unregisterConnection(const std::string& ip);

 private:
  void handleRegister(const std::shared_ptr<Session>& session, const templar::proto::Bytes& payload);
  void handleLogin(const std::shared_ptr<Session>& session, const templar::proto::Bytes& payload);
  void handleFetchPrekeyBundle(const std::shared_ptr<Session>& session,
                               const templar::proto::Bytes& payload);
  void handleSendMsg(const std::shared_ptr<Session>& session, const templar::proto::Bytes& payload);
  void handlePublishOtpk(const std::shared_ptr<Session>& session,
                         const templar::proto::Bytes& payload);
  void handleAck(const std::shared_ptr<Session>& session, const templar::proto::Bytes& payload);
  void handleSubscribePresence(const std::shared_ptr<Session>& session,
                               const templar::proto::Bytes& payload);

  void handleCreateGroup(const std::shared_ptr<Session>& session,
                         const templar::proto::Bytes& payload);
  void handleInviteToGroup(const std::shared_ptr<Session>& session,
                           const templar::proto::Bytes& payload);
  void handleAcceptGroupInvite(const std::shared_ptr<Session>& session,
                               const templar::proto::Bytes& payload);
  void handleRejectGroupInvite(const std::shared_ptr<Session>& session,
                               const templar::proto::Bytes& payload);
  void handleKickFromGroup(const std::shared_ptr<Session>& session,
                           const templar::proto::Bytes& payload);
  void handleSendGroupMsg(const std::shared_ptr<Session>& session,
                          const templar::proto::Bytes& payload);
  void handleLeaveGroup(const std::shared_ptr<Session>& session,
                        const templar::proto::Bytes& payload);

  // --- Blobs de archivo (ver BlobStore.hpp) ---
  void handleUploadBlobBegin(const std::shared_ptr<Session>& session,
                             const templar::proto::Bytes& payload);
  void handleUploadBlobChunk(const std::shared_ptr<Session>& session,
                             const templar::proto::Bytes& payload);
  void handleUploadBlobEnd(const std::shared_ptr<Session>& session,
                           const templar::proto::Bytes& payload);
  void handleDownloadBlob(const std::shared_ptr<Session>& session,
                          const templar::proto::Bytes& payload);

  // --- Limite de intentos de login ---
  // En memoria, por IP -- se reinicia si el servidor se reinicia, y eso es
  // aceptable (no es una lista negra persistente, solo frena una fuerza
  // bruta activa). Se limita por IP y no por username para que probar
  // muchos usernames distintos desde la misma maquina no sea una forma de
  // saltarselo.
  struct LoginAttempts {
    int failures = 0;
    std::chrono::steady_clock::time_point windowStart{};
    std::chrono::steady_clock::time_point lockedUntil{};
  };
  static constexpr int kMaxLoginFailures = 5;
  static constexpr std::chrono::minutes kLoginFailureWindow{5};
  static constexpr std::chrono::seconds kLoginLockout{30};

  bool isLoginLocked(const std::string& ip);
  void recordLoginFailure(const std::string& ip);
  void recordLoginSuccess(const std::string& ip);

  std::mutex loginAttemptsMutex_;
  std::unordered_map<std::string, LoginAttempts> loginAttemptsByIp_;

  // --- Limite de intentos de REGISTRO ---
  // A diferencia del login (donde solo cuentan los fallos, y un exito
  // resetea), aqui cuenta CUALQUIER intento (exito o fallo): una cuenta
  // legitima solo se registra una vez, asi que basta con acotar el
  // volumen bruto desde una IP en una ventana deslizante -- sin timer de
  // bloqueo aparte, mas simple que LoginAttempts a proposito.
  static constexpr size_t kMaxRegisterAttempts = 5;
  static constexpr std::chrono::minutes kRegisterAttemptWindow{60};

  bool isRegisterLocked(const std::string& ip);
  void recordRegisterAttempt(const std::string& ip);

  std::mutex registerAttemptsMutex_;
  std::unordered_map<std::string, std::vector<std::chrono::steady_clock::time_point>>
      registerAttemptsByIp_;

  std::mutex connectionsMutex_;
  std::unordered_map<std::string, int> connectionsByIp_;

  void flushMailbox(const std::shared_ptr<Session>& session);
  void flushGroupInvites(const std::shared_ptr<Session>& session);
  void sendMyGroups(const std::shared_ptr<Session>& session);

  // Comun a handleSendMsg (groupId vacio) y handleSendGroupMsg (groupId no
  // vacio): encola de forma duradera y empuja en vivo si el destinatario
  // esta conectado ahora mismo.
  void relayEncryptedMessage(const std::shared_ptr<Session>& session, const std::string& recipient,
                             bool isFirst, const templar::proto::Bytes& senderIdentityPkX25519,
                             const templar::proto::Bytes& senderEphemeralPk,
                             const templar::proto::Bytes& ciphertext, const std::string& groupId,
                             const templar::proto::Bytes& usedOneTimePrekeyPub);

  void pushGroupInvite(const std::shared_ptr<Session>& session, int64_t inviteId,
                       const std::string& groupId, const std::string& groupName,
                       const std::string& inviter);

  std::shared_ptr<Session> findOnline(const std::string& username);

  // Avisa a todos los suscriptores vivos de `username` que su estado en
  // linea cambio. Poda de paso las suscripciones cuyo Session ya murio.
  void notifyPresenceSubscribers(const std::string& username, bool isOnline);

  Database& db_;
  BlobStore& blobStore_;
  std::mutex onlineMutex_;
  std::unordered_map<std::string, std::weak_ptr<Session>> online_;

  // Suscripciones de presencia: solo en memoria, se pierden al desconectar
  // -- no hay ningun concepto de "lista de contactos" persistente en el
  // servidor, a proposito.
  std::unordered_multimap<std::string, std::weak_ptr<Session>> presenceSubscribers_;
};

}  // namespace templar::server
