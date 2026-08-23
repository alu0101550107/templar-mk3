#include "Session.hpp"

#include <iostream>

#include "Router.hpp"

namespace templar::server {

using namespace templar::proto;
namespace asio = boost::asio;

Session::Session(SslStream socket, Router& router)
    : socket_(std::move(socket)), router_(router), preAuthTimer_(socket_.get_executor()) {
  boost::system::error_code ec;
  auto endpoint = socket_.lowest_layer().remote_endpoint(ec);
  remoteAddress_ = ec ? "desconocida" : endpoint.address().to_string();
}

void Session::start() {
  preAuthTimer_.expires_after(kPreAuthTimeout);
  asio::co_spawn(socket_.get_executor(), [self = shared_from_this()] { return self->run(); },
                 asio::detached);
  asio::co_spawn(
      socket_.get_executor(), [self = shared_from_this()] { return self->watchPreAuthTimeout(); },
      asio::detached);
}

asio::awaitable<void> Session::watchPreAuthTimeout() {
  boost::system::error_code ec;
  co_await preAuthTimer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
  // ec = el timer se cancelo (login/registro exitoso via setUsername(), o
  // la sesion ya se cerro por otro motivo) -- en cualquier caso, nada que
  // hacer. Sin ec = de verdad paso kPreAuthTimeout sin autenticarse.
  if (ec) co_return;

  boost::system::error_code closeEc;
  socket_.lowest_layer().close(closeEc);
}

void Session::deliver(MsgType type, const Bytes& payload) {
  auto self = shared_from_this();
  auto frame = std::make_shared<Bytes>(encodeFrame(type, payload));

  asio::post(socket_.get_executor(), [self, frame] {
    bool writeInProgress = !self->writeQueue_.empty();
    self->writeQueue_.push_back(frame);
    // Si ya habia algo en la cola, esa escritura en curso continuara sola
    // (ver el completion handler de doWrite) hasta llegar a este frame.
    if (!writeInProgress) {
      self->doWrite();
    }
  });
}

void Session::doWrite() {
  auto self = shared_from_this();
  asio::async_write(
      socket_, asio::buffer(*writeQueue_.front()),
      [self](boost::system::error_code ec, size_t) {
        if (ec) {
          // El bucle de lectura (run()) descubrira el mismo fallo al
          // intentar leer y cerrara la sesion; no hace falta duplicarlo.
          return;
        }
        self->writeQueue_.pop_front();
        if (!self->writeQueue_.empty()) {
          self->doWrite();
        }
      });
}

asio::awaitable<void> Session::run() {
  try {
    std::array<uint8_t, 4096> buf;
    for (;;) {
      size_t n = co_await socket_.async_read_some(asio::buffer(buf), asio::use_awaitable);
      parser_.feed(buf.data(), n);

      while (auto frame = parser_.next()) {
        router_.handleFrame(shared_from_this(), frame->type, frame->payload);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[Sesion] Conexion cerrada ('" << (username_.empty() ? "sin login" : username_)
              << "'): " << e.what() << "\n";
  }

  router_.onDisconnect(shared_from_this());

  // Cierre a nivel TCP directamente (sin el close_notify de TLS): esto es
  // una desconexion, no un cierre ordenado, y ya estamos en el manejador de
  // error del bucle de lectura -- intentar un shutdown TLS aqui solo
  // anadiria otra operacion asincrona que puede fallar igual.
  boost::system::error_code ec;
  socket_.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
  socket_.lowest_layer().close(ec);
}

}  // namespace templar::server
