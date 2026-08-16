#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <deque>
#include <memory>
#include <string>

#include "templar/Protocol.hpp"

namespace templar::server {

class Router;

// Una conexión TLS con un cliente (el handshake ya se completo en
// listener(), antes de construir el Session). El socket se crea sobre un
// boost::asio::strand propio (asignado en el accept), así que todas las
// lecturas/escrituras de esta sesión quedan serializadas automáticamente sin
// necesidad de mutex, incluso si el io_context corre en un pool de hilos.
class Session : public std::enable_shared_from_this<Session> {
 public:
  using SslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

  Session(SslStream socket, Router& router);

  void start();

  // Empuja un frame a este cliente. Seguro de llamar desde cualquier hilo:
  // internamente se postea al strand del socket.
  void deliver(templar::proto::MsgType type, const templar::proto::Bytes& payload);

  void setUsername(std::string username) { username_ = std::move(username); }
  const std::string& username() const { return username_; }
  bool isLoggedIn() const { return !username_.empty(); }

  // Direccion IP del peer, capturada una vez al construir la sesion (antes
  // de que el socket pueda cerrarse) -- se usa para el limite de intentos
  // de login. "desconocida" si por lo que sea no se pudo leer.
  const std::string& remoteAddress() const { return remoteAddress_; }

 private:
  boost::asio::awaitable<void> run();

  // Arranca el siguiente envio de la cola si no hay ya uno en curso. Boost.Asio
  // prohibe tener mas de un async_write() en vuelo a la vez sobre el mismo
  // socket -- si deliver() llamara a async_write() directamente cada vez,
  // una rafaga de mensajes seguidos (p.ej. los bloques de un archivo grande)
  // podria solapar dos escrituras y corromper el framing en el receptor.
  // Solo se llama desde el strand del socket, nunca hace falta un mutex.
  void doWrite();

  SslStream socket_;
  Router& router_;
  templar::proto::FrameParser parser_;
  std::string username_;
  std::string remoteAddress_;
  std::deque<std::shared_ptr<templar::proto::Bytes>> writeQueue_;
};

}  // namespace templar::server
