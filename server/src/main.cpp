#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "BlobStore.hpp"
#include "Database.hpp"
#include "Router.hpp"
#include "Session.hpp"

namespace asio = boost::asio;
using asio::ip::tcp;

namespace templar::server {

// Mismo limite que kMaxFileSize en los clientes (client/src/MainWindow.hpp,
// phone/src/ClientController.cpp) -- pero aqui de verdad se aplica, no solo
// por cortesia (un cliente modificado no podria saltarselo: BlobStore lo
// comprueba fragmento a fragmento segun va llegando, sin fiarse del tamano
// declarado en UploadBlobBegin).
constexpr uint64_t kMaxBlobBytes = 200ull * 1024 * 1024;
// Cuanto se guarda un blob sin descargar antes de borrarlo solo (barrido
// periodico, ver blobCleanupLoop).
constexpr std::chrono::hours kBlobRetention{24 * 30};
// Cuota TOTAL por usuario (sumando todos sus blobs, completos o a medio
// subir) -- diez veces el limite de un blob individual, generoso para uso
// normal de un grupo pequeno durante los 30 dias de retencion, acotado
// para cualquier cuenta que abuse. Ver BlobStore::beginUpload.
constexpr uint64_t kMaxBlobBytesPerUser = 2ull * 1024 * 1024 * 1024;

// Directorio de datos estable e independiente del directorio de trabajo:
// si se lanza el servidor desde un directorio de build que luego se borra
// (p.ej. "rm -rf build" para una recompilacion limpia), ni la base de
// cuentas ni el certificado TLS deben irse con el. Sigue el mismo criterio
// XDG que ya usa LocalStore en el cliente.
std::filesystem::path defaultDataDir() {
  const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
  std::filesystem::path dataDir;
  if (xdgDataHome && *xdgDataHome) {
    dataDir = std::filesystem::path(xdgDataHome);
  } else {
    const char* home = std::getenv("HOME");
    dataDir = std::filesystem::path(home ? home : ".") / ".local" / "share";
  }
  dataDir /= "templar";
  std::filesystem::create_directories(dataDir);
  return dataDir;
}

// Si no existe ya un certificado (p.ej. primer arranque, o uno generado a
// mano por setup.sh), se genera uno autofirmado con el openssl del sistema.
// El cliente no valida esto contra ninguna CA -- confia en el (TOFU, como
// SSH) la primera vez que se conecta y avisa fuerte si cambia despues, asi
// que no hace falta un certificado "de verdad", solo uno estable en el
// tiempo para esta instalacion del servidor.
void ensureServerCertificate(const std::filesystem::path& certPath,
                             const std::filesystem::path& keyPath) {
  if (std::filesystem::exists(certPath) && std::filesystem::exists(keyPath)) return;

  std::cout << "[Servidor] No hay certificado TLS todavia, generando uno autofirmado en "
            << certPath.parent_path() << " ...\n";

  std::string cmd = "openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes "
                    "-keyout '" +
                    keyPath.string() + "' -out '" + certPath.string() +
                    "' -subj '/CN=templar-server' >/dev/null 2>&1";
  if (std::system(cmd.c_str()) != 0) {
    throw std::runtime_error(
        "No se pudo generar el certificado TLS (¿esta 'openssl' instalado?). "
        "Tambien puedes generarlo tu mismo con 'openssl req -x509 -newkey rsa:2048 -sha256 "
        "-days 3650 -nodes -keyout " +
        keyPath.string() + " -out " + certPath.string() + " -subj /CN=templar-server'.");
  }
  std::filesystem::permissions(keyPath, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_write);
}

void printCertificateFingerprint(const std::filesystem::path& certPath) {
  std::cout << "[Servidor] Huella del certificado TLS (comparte esto con quien vaya a fiarse "
               "manualmente, aunque el cliente ya la recuerda solo tras la primera conexion):\n";
  std::string cmd = "openssl x509 -in '" + certPath.string() + "' -noout -fingerprint -sha256";
  std::system(cmd.c_str());
}

asio::awaitable<void> listener(tcp::acceptor acceptor, asio::io_context& ioContext,
                               asio::ssl::context& sslContext, Router& router) {
  for (;;) {
    auto strand = asio::make_strand(ioContext.get_executor());
    tcp::socket socket(strand);
    // redirect_error en vez de use_awaitable a secas: un accept() puede
    // fallar por motivos transitorios (un peer que manda RST entre el SYN y
    // el accept() de verdad, o EMFILE si se agotan los descriptores de
    // fichero bajo trafico de escaneo masivo sostenido -- visto en
    // produccion) -- con use_awaitable eso lanzaria una excepcion que, al
    // escapar de esta corrutina lanzada con asio::detached (mas abajo en
    // main()), la mataria en silencio para siempre: el proceso sigue
    // "activo" segun systemd, pero deja de aceptar conexiones nuevas hasta
    // reiniciarlo a mano. Capturando el error aqui, el bucle sigue vivo
    // pase lo que pase.
    boost::system::error_code acceptEc;
    co_await acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, acceptEc));
    if (acceptEc) {
      std::cerr << "[Servidor] Fallo aceptando una conexion, se reintenta: " << acceptEc.message()
                << "\n";
      // Pequena espera antes de reintentar: sin esto, un fallo persistente
      // (p.ej. EMFILE mientras el limite de descriptores siga agotado)
      // convertiria el bucle en un spin sin freno consumiendo CPU y
      // llenando el log.
      asio::steady_timer backoff(ioContext);
      backoff.expires_after(std::chrono::milliseconds(200));
      boost::system::error_code waitEc;
      co_await backoff.async_wait(asio::redirect_error(asio::use_awaitable, waitEc));
      continue;
    }

    // remote_endpoint() es sincrono y SI puede lanzar (p.ej. si el peer ya
    // cerro la conexion en el intervalo entre que accept() completo y esta
    // linea) -- mismo riesgo que el accept() de arriba de matar el listener
    // entero si escapa sin capturar, asi que se trata igual: se loguea y se
    // sigue con la siguiente conexion en vez de dejar que la excepcion suba.
    std::string ip;
    uint16_t remotePort = 0;
    try {
      auto endpoint = socket.remote_endpoint();
      ip = endpoint.address().to_string();
      remotePort = endpoint.port();
    } catch (const std::exception& e) {
      std::cerr << "[Servidor] Conexion aceptada pero ya cerrada al leer su direccion: " << e.what()
                << "\n";
      continue;
    }
    std::cout << "[Servidor] Nueva conexion desde " << ip << ":" << remotePort << "\n";

    // Se rechaza ANTES del handshake TLS (mas barato) si esta IP ya tiene
    // demasiadas conexiones abiertas a la vez -- ver Router::kMaxConnectionsPerIp.
    if (!router.tryRegisterConnection(ip)) {
      std::cerr << "[Servidor] Demasiadas conexiones concurrentes desde " << ip
                << ", rechazada.\n";
      continue;
    }

    asio::ssl::stream<tcp::socket> sslSocket(std::move(socket), sslContext);
    boost::system::error_code handshakeEc;
    co_await sslSocket.async_handshake(asio::ssl::stream_base::server,
                                       asio::redirect_error(asio::use_awaitable, handshakeEc));
    if (handshakeEc) {
      std::cerr << "[Servidor] Fallo el handshake TLS con " << ip << ": " << handshakeEc.message()
                << "\n";
      router.unregisterConnection(ip);
      continue;
    }

    std::make_shared<Session>(std::move(sslSocket), router)->start();
  }
}

// Barrido periodico de blobs caducados (ver BlobStore::cleanupExpired) --
// una corrutina mas, en el mismo io_context que atiende conexiones, en vez
// de un hilo aparte: coherente con como ya esta hecho el resto del
// servidor (nada de infraestructura de "jobs en segundo plano" separada
// para una tarea tan simple).
asio::awaitable<void> blobCleanupLoop(asio::io_context& ioContext, BlobStore& blobStore) {
  asio::steady_timer timer(ioContext);
  for (;;) {
    timer.expires_after(std::chrono::hours(6));
    boost::system::error_code ec;
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    if (ec) co_return;  // timer cancelado (p.ej. apagado) -- no es un error real

    size_t removed = blobStore.cleanupExpired();
    if (removed > 0) {
      std::cout << "[Servidor] Limpieza de blobs: " << removed
                << " archivo(s) caducado(s) borrado(s)\n";
    }
  }
}

}  // namespace templar::server

int main(int argc, char* argv[]) {
  using namespace templar::server;

  uint16_t port = 8080;
  std::filesystem::path dataDir = defaultDataDir();
  std::string dbPath = (dataDir / "templar.db").string();
  // Por defecto escucha en todas las interfaces (0.0.0.0) -- el tercer
  // argumento opcional deja restringirlo (p.ej. a 127.0.0.1 si va a correr
  // detras de un reverse proxy propio en la misma maquina).
  std::string bindAddress = "0.0.0.0";
  if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));
  if (argc > 2) dbPath = argv[2];
  if (argc > 3) bindAddress = argv[3];

  try {
    std::filesystem::path certPath = dataDir / "cert.pem";
    std::filesystem::path keyPath = dataDir / "key.pem";
    ensureServerCertificate(certPath, keyPath);
    printCertificateFingerprint(certPath);

    asio::ssl::context sslContext(asio::ssl::context::tls_server);
    // Fija TLS 1.2 como minimo (excluye SSLv2/SSLv3/TLS1.0/TLS1.1
    // explicitamente, en vez de depender de que la politica del OpenSSL
    // del sistema ya los excluya por su cuenta). Los clientes de este
    // proyecto (Qt/OpenSSL, desktop y movil) negocian TLS 1.2/1.3 sin
    // problema.
    sslContext.set_options(asio::ssl::context::default_workarounds |
                           asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
                           asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
    sslContext.use_certificate_chain_file(certPath.string());
    sslContext.use_private_key_file(keyPath.string(), asio::ssl::context::pem);

    Database db(dbPath);
    std::string blobsDir = (dataDir / "blobs").string();
    BlobStore blobStore(db, blobsDir, kMaxBlobBytes, kMaxBlobBytesPerUser, kBlobRetention);
    Router router(db, blobStore);

    unsigned int numThreads = std::max(2u, std::thread::hardware_concurrency());
    asio::io_context ioContext(static_cast<int>(numThreads));

    tcp::acceptor acceptor(ioContext,
                           tcp::endpoint(asio::ip::make_address(bindAddress), port));
    asio::co_spawn(ioContext, listener(std::move(acceptor), ioContext, sslContext, router),
                   asio::detached);
    asio::co_spawn(ioContext, blobCleanupLoop(ioContext, blobStore), asio::detached);

    std::cout << "[Servidor] Templar mk3 escuchando en " << bindAddress << ":" << port << " ("
              << numThreads << " hilos, DB: " << dbPath << ", blobs: " << blobsDir
              << ", TLS activo)\n";

    std::vector<std::thread> pool;
    for (unsigned int i = 1; i < numThreads; ++i) {
      pool.emplace_back([&ioContext] { ioContext.run(); });
    }
    ioContext.run();
    for (auto& t : pool) t.join();
  } catch (const std::exception& e) {
    std::cerr << "[Error fatal] " << e.what() << "\n";
    return 1;
  }

  return 0;
}
