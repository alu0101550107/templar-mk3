#include "Router.hpp"

#include <sodium.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include "Session.hpp"
#include "templar/crypto/Identity.hpp"

namespace templar::server {

using namespace templar::proto;

namespace {

// Un mensaje de chat cifrado real (texto, o un FileBlobPointer -- que es
// solo un puntero pequeno, el archivo en si va por BlobStore) nunca se
// acerca a esto -- ver el comentario junto a su uso en handleSendMsg/
// handleSendGroupMsg.
constexpr size_t kMaxChatMessageBytes = 1024 * 1024;

void sendErr(const std::shared_ptr<Session>& session, MsgType type, const std::string& reason) {
  Writer w;
  w.str(reason);
  session->deliver(type, w.take());
}

void sendGroupErr(const std::shared_ptr<Session>& session, const std::string& context,
                  const std::string& reason) {
  Writer w;
  w.str(context);
  w.str(reason);
  session->deliver(MsgType::GroupErr, w.take());
}

}  // namespace

Router::Router(Database& db, BlobStore& blobStore) : db_(db), blobStore_(blobStore) {
  templar::crypto::initSodium();
}

void Router::handleFrame(const std::shared_ptr<Session>& session, MsgType type,
                         const Bytes& payload) {
  try {
    switch (type) {
      case MsgType::Register:
        handleRegister(session, payload);
        break;
      case MsgType::Login:
        handleLogin(session, payload);
        break;
      case MsgType::FetchPrekeyBundle:
        handleFetchPrekeyBundle(session, payload);
        break;
      case MsgType::SendMsg:
        handleSendMsg(session, payload);
        break;
      case MsgType::Ack:
        handleAck(session, payload);
        break;
      case MsgType::SubscribePresence:
        handleSubscribePresence(session, payload);
        break;
      case MsgType::CreateGroup:
        handleCreateGroup(session, payload);
        break;
      case MsgType::InviteToGroup:
        handleInviteToGroup(session, payload);
        break;
      case MsgType::AcceptGroupInvite:
        handleAcceptGroupInvite(session, payload);
        break;
      case MsgType::RejectGroupInvite:
        handleRejectGroupInvite(session, payload);
        break;
      case MsgType::KickFromGroup:
        handleKickFromGroup(session, payload);
        break;
      case MsgType::SendGroupMsg:
        handleSendGroupMsg(session, payload);
        break;
      case MsgType::PublishOtpk:
        handlePublishOtpk(session, payload);
        break;
      case MsgType::LeaveGroup:
        handleLeaveGroup(session, payload);
        break;
      case MsgType::UploadBlobBegin:
        handleUploadBlobBegin(session, payload);
        break;
      case MsgType::UploadBlobChunk:
        handleUploadBlobChunk(session, payload);
        break;
      case MsgType::UploadBlobEnd:
        handleUploadBlobEnd(session, payload);
        break;
      case MsgType::DownloadBlob:
        handleDownloadBlob(session, payload);
        break;
      default:
        std::cerr << "[Router] Tipo de mensaje no soportado en Fase 1: "
                  << static_cast<int>(type) << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "[Router] Error procesando frame de '"
              << (session->username().empty() ? "?" : session->username()) << "': " << e.what()
              << "\n";
  }
}

bool Router::isRegisterLocked(const std::string& ip) {
  std::lock_guard<std::mutex> lock(registerAttemptsMutex_);
  auto it = registerAttemptsByIp_.find(ip);
  if (it == registerAttemptsByIp_.end()) return false;

  auto now = std::chrono::steady_clock::now();
  auto& timestamps = it->second;
  timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
                                  [&](auto t) { return now - t > kRegisterAttemptWindow; }),
                   timestamps.end());
  return timestamps.size() >= kMaxRegisterAttempts;
}

void Router::recordRegisterAttempt(const std::string& ip) {
  std::lock_guard<std::mutex> lock(registerAttemptsMutex_);
  registerAttemptsByIp_[ip].push_back(std::chrono::steady_clock::now());
}

bool Router::tryRegisterConnection(const std::string& ip) {
  std::lock_guard<std::mutex> lock(connectionsMutex_);
  int& count = connectionsByIp_[ip];
  if (count >= kMaxConnectionsPerIp) return false;
  ++count;
  return true;
}

void Router::unregisterConnection(const std::string& ip) {
  std::lock_guard<std::mutex> lock(connectionsMutex_);
  auto it = connectionsByIp_.find(ip);
  if (it == connectionsByIp_.end()) return;
  if (--it->second <= 0) connectionsByIp_.erase(it);
}

void Router::handleRegister(const std::shared_ptr<Session>& session, const Bytes& payload) {
  const std::string& ip = session->remoteAddress();
  if (isRegisterLocked(ip)) {
    // Mismo criterio que el log de login fallido de arriba -- formato
    // estable para que fail2ban lo banee por firewall.
    std::cerr << "[Servidor] Registro rechazado (limite de tasa) desde " << ip << "\n";
    sendErr(session, MsgType::RegisterErr,
           "Demasiados registros desde esta direccion. Espera un momento antes de volver a "
           "intentarlo.");
    return;
  }
  recordRegisterAttempt(ip);

  Reader r(payload);
  std::string username = r.str();
  std::string password = r.str();
  Bytes identityPkX25519 = r.blob();
  Bytes identityPkEd25519 = r.blob();
  Bytes signedPrekeyPub = r.blob();
  Bytes signedPrekeySig = r.blob();

  if (username.empty() || username.size() > 64 || password.size() < 8 ||
      identityPkX25519.size() != crypto_box_PUBLICKEYBYTES ||
      identityPkEd25519.size() != crypto_sign_PUBLICKEYBYTES ||
      signedPrekeyPub.size() != crypto_box_PUBLICKEYBYTES ||
      signedPrekeySig.size() != crypto_sign_BYTES) {
    sendErr(session, MsgType::RegisterErr, "Datos de registro invalidos.");
    return;
  }

  unsigned char signerPk[crypto_sign_PUBLICKEYBYTES];
  std::copy(identityPkEd25519.begin(), identityPkEd25519.end(), signerPk);
  if (!templar::crypto::verify(signerPk, signedPrekeyPub, signedPrekeySig)) {
    sendErr(session, MsgType::RegisterErr, "La firma de la signed prekey no es valida.");
    return;
  }

  // La contrasena viaja cifrada por el TLS de transporte (ver main.cpp) --
  // aqui solo se protege ademas con Argon2id antes de guardarla, para que
  // ni el propio operador del servidor pueda leerla en la base de datos.
  char hashBuf[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(hashBuf, password.c_str(), password.size(),
                        crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
    sendErr(session, MsgType::RegisterErr, "Fallo interno al proteger la contrasena.");
    return;
  }

  bool created = db_.createUser(username, hashBuf, identityPkX25519, identityPkEd25519,
                                signedPrekeyPub, signedPrekeySig);
  if (!created) {
    sendErr(session, MsgType::RegisterErr, "El nombre de usuario ya esta en uso.");
    return;
  }

  session->deliver(MsgType::RegisterOk, {});
}

bool Router::isLoginLocked(const std::string& ip) {
  std::lock_guard<std::mutex> lock(loginAttemptsMutex_);
  auto it = loginAttemptsByIp_.find(ip);
  if (it == loginAttemptsByIp_.end()) return false;
  return std::chrono::steady_clock::now() < it->second.lockedUntil;
}

void Router::recordLoginFailure(const std::string& ip) {
  std::lock_guard<std::mutex> lock(loginAttemptsMutex_);
  auto now = std::chrono::steady_clock::now();
  auto& state = loginAttemptsByIp_[ip];

  if (state.windowStart == std::chrono::steady_clock::time_point{} ||
      now - state.windowStart > kLoginFailureWindow) {
    state.failures = 0;
    state.windowStart = now;
  }

  ++state.failures;
  if (state.failures >= kMaxLoginFailures) {
    state.lockedUntil = now + kLoginLockout;
    // Se reinicia el conteo: al expirar el bloqueo, empieza una ventana
    // nueva en vez de bloquear de nuevo instantaneamente.
    state.failures = 0;
    state.windowStart = now;
  }
}

void Router::recordLoginSuccess(const std::string& ip) {
  std::lock_guard<std::mutex> lock(loginAttemptsMutex_);
  loginAttemptsByIp_.erase(ip);
}

void Router::handleLogin(const std::shared_ptr<Session>& session, const Bytes& payload) {
  Reader r(payload);
  std::string username = r.str();
  std::string password = r.str();

  const std::string& ip = session->remoteAddress();
  if (isLoginLocked(ip)) {
    sendErr(session, MsgType::LoginErr,
           "Demasiados intentos fallidos. Espera un momento antes de volver a intentarlo.");
    return;
  }

  auto user = db_.findUser(username);
  // Mensaje de error genérico en ambos casos (usuario inexistente / password
  // incorrecta) para no filtrar qué usuarios existen.
  const char* genericErr = "Usuario o contrasena incorrectos.";

  if (!user || crypto_pwhash_str_verify(user->passwordHash.c_str(), password.c_str(),
                                        password.size()) != 0) {
    recordLoginFailure(ip);
    // Formato estable a proposito -- es la linea que un filtro de fail2ban
    // (ver deploy_server.sh/docs) busca para banear por firewall a quien
    // insiste con credenciales invalidas, por debajo del rate-limit interno
    // de arriba (que solo retrasa, no bloquea a nivel de red).
    std::cerr << "[Servidor] Login fallido desde " << ip << "\n";
    sendErr(session, MsgType::LoginErr, genericErr);
    return;
  }
  recordLoginSuccess(ip);

  session->setUsername(username);
  {
    std::lock_guard<std::mutex> lock(onlineMutex_);
    online_[username] = session;
  }

  session->deliver(MsgType::LoginOk, {});
  sendMyGroups(session);
  flushGroupInvites(session);
  flushMailbox(session);
  notifyPresenceSubscribers(username, true);
}

void Router::handleFetchPrekeyBundle(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string target = r.str();

  auto user = db_.findUser(target);
  if (!user) {
    sendErr(session, MsgType::PrekeyBundleErr, "Usuario '" + target + "' no encontrado.");
    return;
  }

  // Se entrega como mucho una one-time prekey por bundle, y se borra al
  // entregarla -- si no queda ninguna, el iniciador simplemente cae al
  // modo de 3-DH (ver PrekeyBundle::hasOneTimePrekey en el cliente).
  std::optional<Bytes> otpk = db_.takeOneTimePrekey(target);

  Writer w;
  w.blob(user->identityPkX25519);
  w.blob(user->identityPkEd25519);
  w.blob(user->signedPrekeyPub);
  w.blob(user->signedPrekeySig);
  w.blob(otpk.value_or(Bytes()));
  session->deliver(MsgType::PrekeyBundle, w.take());
}

void Router::handlePublishOtpk(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  uint32_t count = r.u32();
  if (count > 200) return;  // cota de seguridad ante un cliente que mande un lote absurdo

  std::vector<Bytes> publicKeys;
  publicKeys.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Bytes pub = r.blob();
    if (pub.size() != crypto_box_PUBLICKEYBYTES) continue;  // descarta silenciosamente lo invalido
    publicKeys.push_back(std::move(pub));
  }

  db_.addOneTimePrekeys(session->username(), publicKeys);
}

void Router::handleSendMsg(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string recipient = r.str();
  bool isFirst = r.u8() != 0;
  Bytes senderIdentityPkX25519 = r.blob();
  Bytes senderEphemeralPk = r.blob();
  Bytes ciphertext = r.blob();
  Bytes usedOneTimePrekeyPub = r.blob();

  // Un mensaje de texto cifrado real nunca se acerca a esto -- sin este
  // limite, alguien podria usar SendMsg como canal alternativo para volcar
  // hasta 16 MiB (el tope de un frame entero) por mensaje en el buzon de
  // una victima, sin pasar por BlobStore ni su propio limite de tamano.
  if (ciphertext.size() > kMaxChatMessageBytes) return;

  relayEncryptedMessage(session, recipient, isFirst, senderIdentityPkX25519, senderEphemeralPk,
                        ciphertext, "", usedOneTimePrekeyPub);
}

void Router::handleSendGroupMsg(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string groupId = r.str();
  std::string recipient = r.str();
  bool isFirst = r.u8() != 0;
  Bytes senderIdentityPkX25519 = r.blob();
  Bytes senderEphemeralPk = r.blob();
  Bytes ciphertext = r.blob();
  Bytes usedOneTimePrekeyPub = r.blob();

  if (ciphertext.size() > kMaxChatMessageBytes) return;

  // Autorizacion server-side: tanto quien manda como el destinatario deben
  // ser miembros actuales del grupo. El cliente hace un envio (fan-out) por
  // cada miembro, pero es el servidor quien decide si eso es legitimo.
  if (!db_.isGroupMember(groupId, session->username())) {
    std::cerr << "[Router] SendGroupMsg: '" << session->username() << "' no es miembro de '"
              << groupId << "'\n";
    return;
  }
  if (!db_.isGroupMember(groupId, recipient)) {
    std::cerr << "[Router] SendGroupMsg: destinatario '" << recipient << "' no es miembro de '"
              << groupId << "'\n";
    return;
  }

  relayEncryptedMessage(session, recipient, isFirst, senderIdentityPkX25519, senderEphemeralPk,
                        ciphertext, groupId, usedOneTimePrekeyPub);
}

void Router::relayEncryptedMessage(const std::shared_ptr<Session>& session,
                                   const std::string& recipient, bool isFirst,
                                   const Bytes& senderIdentityPkX25519,
                                   const Bytes& senderEphemeralPk, const Bytes& ciphertext,
                                   const std::string& groupId,
                                   const Bytes& usedOneTimePrekeyPub) {
  int64_t mailboxId;
  try {
    mailboxId = db_.enqueueMessage(recipient, session->username(), isFirst, senderIdentityPkX25519,
                                   senderEphemeralPk, ciphertext, groupId, usedOneTimePrekeyPub);
  } catch (const std::exception& e) {
    std::cerr << "[Router] " << (groupId.empty() ? "SendMsg" : "SendGroupMsg")
              << " fallo: " << e.what() << "\n";
    return;
  }

  Writer w;
  w.u32(static_cast<uint32_t>(mailboxId));
  if (!groupId.empty()) w.str(groupId);
  w.str(session->username());
  w.u8(isFirst ? 1 : 0);
  w.blob(senderIdentityPkX25519);
  w.blob(senderEphemeralPk);
  w.blob(ciphertext);
  w.blob(usedOneTimePrekeyPub);
  Bytes deliverPayload = w.take();

  if (auto recipientSession = findOnline(recipient)) {
    recipientSession->deliver(groupId.empty() ? MsgType::DeliverMsg : MsgType::DeliverGroupMsg,
                              deliverPayload);
  }
  // Si el destinatario no está conectado ahora mismo, el mensaje ya quedó
  // en `mailbox` y se entregará en su próximo login (flushMailbox).
}

void Router::handleSubscribePresence(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string target = r.str();

  bool isOnline;
  {
    std::lock_guard<std::mutex> lock(onlineMutex_);
    presenceSubscribers_.emplace(target, session);
    auto it = online_.find(target);
    isOnline = it != online_.end() && it->second.lock() != nullptr;
  }

  Writer w;
  w.str(target);
  w.u8(isOnline ? 1 : 0);
  session->deliver(MsgType::PresenceUpdate, w.take());
}

void Router::notifyPresenceSubscribers(const std::string& username, bool isOnline) {
  Writer w;
  w.str(username);
  w.u8(isOnline ? 1 : 0);
  Bytes payload = w.take();

  std::lock_guard<std::mutex> lock(onlineMutex_);
  auto range = presenceSubscribers_.equal_range(username);
  for (auto it = range.first; it != range.second;) {
    if (auto subscriber = it->second.lock()) {
      subscriber->deliver(MsgType::PresenceUpdate, payload);
      ++it;
    } else {
      it = presenceSubscribers_.erase(it);  // poda suscripciones cuyo Session ya murio
    }
  }
}

void Router::handleAck(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  uint32_t mailboxId = r.u32();
  db_.ackMessage(mailboxId, session->username());
}

void Router::flushMailbox(const std::shared_ptr<Session>& session) {
  for (const auto& row : db_.fetchPending(session->username())) {
    Writer w;
    w.u32(static_cast<uint32_t>(row.id));
    if (!row.groupId.empty()) w.str(row.groupId);
    w.str(row.senderUsername);
    w.u8(row.isFirst ? 1 : 0);
    w.blob(row.senderIdentityPkX25519);
    w.blob(row.senderEphemeralPk);
    w.blob(row.ciphertext);
    w.blob(row.usedOneTimePrekeyPub);
    session->deliver(row.groupId.empty() ? MsgType::DeliverMsg : MsgType::DeliverGroupMsg,
                     w.take());
  }
}

void Router::flushGroupInvites(const std::shared_ptr<Session>& session) {
  for (const auto& row : db_.fetchPendingInvites(session->username())) {
    pushGroupInvite(session, row.id, row.groupId, row.groupName, row.inviterUsername);
  }
}

void Router::sendMyGroups(const std::shared_ptr<Session>& session) {
  auto groups = db_.listGroupsForUser(session->username());

  Writer w;
  w.u32(static_cast<uint32_t>(groups.size()));
  for (const auto& gwm : groups) {
    w.str(gwm.group.id);
    w.str(gwm.group.name);
    w.str(gwm.group.adminUsername);
    w.u32(static_cast<uint32_t>(gwm.members.size()));
    for (const auto& member : gwm.members) w.str(member);
  }
  session->deliver(MsgType::MyGroups, w.take());
}

void Router::pushGroupInvite(const std::shared_ptr<Session>& session, int64_t inviteId,
                             const std::string& groupId, const std::string& groupName,
                             const std::string& inviter) {
  Writer w;
  w.u32(static_cast<uint32_t>(inviteId));
  w.str(groupId);
  w.str(groupName);
  w.str(inviter);
  session->deliver(MsgType::GroupInvite, w.take());
}

std::shared_ptr<Session> Router::findOnline(const std::string& username) {
  std::lock_guard<std::mutex> lock(onlineMutex_);
  auto it = online_.find(username);
  if (it == online_.end()) return nullptr;
  return it->second.lock();
}

void Router::handleCreateGroup(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string name = r.str();
  if (name.empty() || name.size() > 128) {
    sendGroupErr(session, "create", "Nombre de grupo invalido.");
    return;
  }

  std::string groupId = db_.createGroup(name, session->username());

  Writer w;
  w.str(groupId);
  w.str(name);
  session->deliver(MsgType::GroupCreated, w.take());
}

void Router::handleInviteToGroup(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string groupId = r.str();
  std::string invitee = r.str();

  auto group = db_.findGroup(groupId);
  if (!group) {
    sendGroupErr(session, "invite", "El grupo ya no existe.");
    return;
  }
  if (group->adminUsername != session->username()) {
    sendGroupErr(session, "invite", "Solo el admin del grupo puede invitar.");
    return;
  }
  if (!db_.findUser(invitee)) {
    sendGroupErr(session, "invite", "El usuario '" + invitee + "' no existe.");
    return;
  }
  if (db_.isGroupMember(groupId, invitee)) {
    sendGroupErr(session, "invite", "'" + invitee + "' ya es miembro del grupo.");
    return;
  }

  int64_t inviteId;
  try {
    inviteId = db_.enqueueGroupInvite(groupId, invitee, session->username());
  } catch (const std::exception& e) {
    sendGroupErr(session, "invite", e.what());
    return;
  }

  if (auto inviteeSession = findOnline(invitee)) {
    pushGroupInvite(inviteeSession, inviteId, groupId, group->name, session->username());
  }
  // Si esta offline, se le manda al hacer login (flushGroupInvites).
}

void Router::handleAcceptGroupInvite(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  uint32_t inviteId = r.u32();

  auto groupId = db_.resolveInvite(inviteId, session->username());
  if (!groupId) return;  // invitacion inexistente o de otro usuario -- se ignora

  db_.addGroupMember(*groupId, session->username());

  // El recien llegado no tenia nombre/admin/lista de miembros de este grupo
  // (solo sabia el id y el nombre por la invitacion) -- se le manda el
  // snapshot completo, igual que al hacer login, para que quede consistente
  // de una sola vez en vez de reconstruirlo a partir de deltas.
  sendMyGroups(session);

  // Avisa al resto de miembros ya existentes para que refresquen su lista
  // en tiempo real si estan online (el recien llegado tambien esta en esta
  // lista y se auto-notifica, pero es un no-op inofensivo del lado cliente).
  Writer w;
  w.str(*groupId);
  w.str(session->username());
  Bytes notif = w.take();
  for (const auto& member : db_.listGroupMembers(*groupId)) {
    if (auto memberSession = findOnline(member)) {
      memberSession->deliver(MsgType::GroupMemberJoined, notif);
    }
  }
}

void Router::handleRejectGroupInvite(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  uint32_t inviteId = r.u32();
  db_.resolveInvite(inviteId, session->username());  // solo borra, no une al grupo
}

void Router::handleKickFromGroup(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string groupId = r.str();
  std::string target = r.str();

  auto group = db_.findGroup(groupId);
  if (!group) {
    sendGroupErr(session, "kick", "El grupo ya no existe.");
    return;
  }
  if (group->adminUsername != session->username()) {
    sendGroupErr(session, "kick", "Solo el admin del grupo puede expulsar.");
    return;
  }
  if (target == group->adminUsername) {
    sendGroupErr(session, "kick", "El admin no puede expulsarse a si mismo.");
    return;
  }
  if (!db_.isGroupMember(groupId, target)) {
    sendGroupErr(session, "kick", "'" + target + "' no es miembro del grupo.");
    return;
  }

  // Se captura la lista ANTES de borrar: el propio expulsado tambien debe
  // enterarse (para quitar el grupo de su barra lateral), y ya no
  // aparecera en group_members despues de removeGroupMember.
  std::vector<std::string> membersBeforeKick = db_.listGroupMembers(groupId);
  db_.removeGroupMember(groupId, target);

  Writer w;
  w.str(groupId);
  w.str(target);
  Bytes notif = w.take();
  for (const auto& member : membersBeforeKick) {
    if (auto memberSession = findOnline(member)) {
      memberSession->deliver(MsgType::GroupMemberKicked, notif);
    }
  }
}

void Router::handleLeaveGroup(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string groupId = r.str();

  auto group = db_.findGroup(groupId);
  if (!group) return;
  if (!db_.isGroupMember(groupId, session->username())) return;

  // Se captura ANTES de borrar (mismo motivo que en el kick): hace falta
  // saber quien quedaba para poder avisarles.
  std::vector<std::string> membersBefore = db_.listGroupMembers(groupId);
  db_.removeGroupMember(groupId, session->username());

  std::vector<std::string> remaining;
  for (const auto& member : membersBefore) {
    if (member != session->username()) remaining.push_back(member);
  }

  if (remaining.empty()) {
    // Nadie mas en el grupo: no tiene sentido dejarlo huerfano.
    db_.deleteGroup(groupId);
    return;
  }

  std::string currentAdmin = group->adminUsername;
  if (currentAdmin == session->username()) {
    // El admin se va y quedan mas personas: se promueve automaticamente al
    // mas antiguo de los que quedan (membersBefore ya viene ordenado por
    // joined_at, y remaining preserva ese orden al filtrar).
    currentAdmin = remaining.front();
    db_.setGroupAdmin(groupId, currentAdmin);
  }

  Writer w;
  w.str(groupId);
  w.str(session->username());
  w.str(currentAdmin);
  Bytes notif = w.take();
  for (const auto& member : remaining) {
    if (auto memberSession = findOnline(member)) {
      memberSession->deliver(MsgType::GroupMemberLeft, notif);
    }
  }
  // El que se va no recibe esta notificacion -- ya actualizo su propia UI
  // de inmediato al mandar LeaveGroup, sin esperar al servidor.
}

void Router::handleUploadBlobBegin(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  uint64_t totalSize = r.u64();
  r.str();  // filename: solo informativo (el cliente lo repite en el error si hace falta),
            // no se guarda -- el nombre real viaja dentro de FileBlobPointer, cifrado.

  try {
    std::string blobId = blobStore_.beginUpload(session->username(), totalSize);
    Writer w;
    w.str(blobId);
    session->deliver(MsgType::UploadBlobBeginOk, w.take());
  } catch (const std::exception& e) {
    sendErr(session, MsgType::UploadBlobBeginErr, e.what());
  }
}

void Router::handleUploadBlobChunk(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string blobId = r.str();
  Bytes chunk = r.blob();

  try {
    blobStore_.appendChunk(blobId, session->username(), chunk);
  } catch (const std::exception& e) {
    // Sin respuesta de error dedicada por fragmento (a diferencia de
    // Begin/End) -- mismo criterio que SendGroupMsg: un fragmento suelto
    // rechazado no es motivo para colgar la conexion, y UploadBlobEnd (que
    // si tiene su propio error) es donde el cliente se entera de que algo
    // fue mal en general.
    std::cerr << "[Router] UploadBlobChunk fallo: " << e.what() << "\n";
  }
}

void Router::handleUploadBlobEnd(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string blobId = r.str();

  try {
    blobStore_.finishUpload(blobId, session->username());
    Writer w;
    w.str(blobId);
    session->deliver(MsgType::UploadBlobEndOk, w.take());
  } catch (const std::exception& e) {
    sendErr(session, MsgType::UploadBlobEndErr, e.what());
  }
}

void Router::handleDownloadBlob(const std::shared_ptr<Session>& session, const Bytes& payload) {
  if (!session->isLoggedIn()) return;

  Reader r(payload);
  std::string blobId = r.str();

  auto path = blobStore_.pathForCompletedBlob(blobId);
  std::ifstream in;
  if (path) in.open(*path, std::ios::binary);
  if (!path || !in) {
    Writer w;
    w.str(blobId);
    w.str("No existe, no termino de subirse, o ya caduco.");
    session->deliver(MsgType::BlobNotFound, w.take());
    return;
  }

  // Lectura y envio sincronos, fragmento a fragmento -- igual de simple
  // que el resto del servidor (nada de hilos dedicados ni E/S asincrona
  // de disco); para el volumen que se espera aqui (chat de grupo pequeno,
  // no un servicio de archivos de verdad) es mas que suficiente.
  //
  // Cada fragmento se releee respetando el prefijo de longitud que escribio
  // BlobStore::appendChunk -- NO en trozos de tamano fijo, porque cada
  // fragmento es un "mensaje" independiente de crypto_secretstream en el
  // cliente, y su pull() exige recibir exactamente los mismos limites que
  // uso el push() original (ver el comentario de appendChunk).
  while (in) {
    unsigned char lenBuf[4];
    in.read(reinterpret_cast<char*>(lenBuf), 4);
    if (in.gcount() == 0) break;  // fin de archivo limpio, entre fragmentos
    if (in.gcount() != 4) break;  // archivo truncado a mitad de un prefijo
    uint32_t chunkLen = (uint32_t(lenBuf[0]) << 24) | (uint32_t(lenBuf[1]) << 16) |
                        (uint32_t(lenBuf[2]) << 8) | uint32_t(lenBuf[3]);

    Bytes chunk(chunkLen);
    if (chunkLen > 0) {
      in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunkLen));
      if (static_cast<uint32_t>(in.gcount()) != chunkLen) break;  // truncado a mitad de fragmento
    }

    Writer w;
    w.str(blobId);
    w.blob(chunk);
    session->deliver(MsgType::BlobData, w.take());
  }

  Writer end;
  end.str(blobId);
  session->deliver(MsgType::BlobEnd, end.take());
}

void Router::onDisconnect(const std::shared_ptr<Session>& session) {
  unregisterConnection(session->remoteAddress());

  if (!session->isLoggedIn()) return;

  {
    std::lock_guard<std::mutex> lock(onlineMutex_);
    auto it = online_.find(session->username());
    if (it != online_.end() && it->second.lock() == session) {
      online_.erase(it);
    }
  }
  notifyPresenceSubscribers(session->username(), false);
}

}  // namespace templar::server
