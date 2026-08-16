// Prueba de integracion de extremo a extremo: levanta dos "clientes" (Alice
// y Bob) reusando las mismas clases de produccion (NetworkManager +
// CryptoEngine) que usa MainWindow, y verifica que:
//   1. Ambos pueden registrarse e iniciar sesion contra un templar_server real.
//   2. Alice puede iniciar una conversacion nueva con Bob (X3DH via
//      FetchPrekeyBundle + PrekeyBundle) y el mensaje llega descifrado.
//   3. Bob puede responder reusando la sesion ya establecida (sin necesitar
//      su propio FetchPrekeyBundle) y la respuesta llega descifrada a Alice.
//   4. Varias rondas de ida y vuelta funcionan seguidas -- ejercita el
//      Double Ratchet real (nuevo par DH en cada turno) sobre la red, no
//      solo en los tests aislados de crypto/tests/.
//
// Se espera que el servidor ya este corriendo en 127.0.0.1:<puerto>, pasado
// como argv[1].

#include <QCoreApplication>
#include <QTimer>

#include <iostream>

#include "CryptoEngine.hpp"
#include "MessagePayload.hpp"
#include "NetworkManager.hpp"
#include "templar/Wire.hpp"

using namespace templar::client;
using namespace templar::proto;
using templar::crypto::PrekeyBundle;

namespace {

Bytes buildRegisterPayload(const CryptoEngine& c, const std::string& username,
                          const std::string& password) {
  const auto& ex = c.identity().exchangeKeys();
  const auto& ed = c.identity().signingKeys();
  const auto& spk = c.signedPrekey();

  Writer w;
  w.str(username);
  w.str(password);
  w.blob(ex.pk, crypto_box_PUBLICKEYBYTES);
  w.blob(ed.pk, crypto_sign_PUBLICKEYBYTES);
  w.blob(spk.pk, crypto_box_PUBLICKEYBYTES);
  w.blob(c.signedPrekeySignature());
  return w.take();
}

Bytes buildLoginPayload(const std::string& username, const std::string& password) {
  Writer w;
  w.str(username);
  w.str(password);
  return w.take();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Uso: " << argv[0] << " <puerto>\n";
    return 2;
  }
  templar::crypto::initSodium();

  QCoreApplication app(argc, argv);
  quint16 port = static_cast<quint16>(std::stoi(argv[1]));

  NetworkManager netA, netB;
  CryptoEngine cryptoA, cryptoB;

  bool aliceLoggedIn = false, bobLoggedIn = false;
  constexpr int kTotalRounds = 4;
  int roundsCompleted = 0;

  auto fail = [&](const QString& reason) {
    std::cerr << "[FAIL] " << reason.toStdString() << "\n";
    QCoreApplication::exit(1);
  };

  auto maybeStartConversation = [&]() {
    if (aliceLoggedIn && bobLoggedIn) {
      Writer w;
      w.str("bob_e2e");
      netA.sendFrame(MsgType::FetchPrekeyBundle, w.take());
    }
  };

  QObject::connect(&netA, &NetworkManager::connectionError,
                   [&](QString m) { fail("netA error: " + m); });
  QObject::connect(&netB, &NetworkManager::connectionError,
                   [&](QString m) { fail("netB error: " + m); });

  QObject::connect(&netA, &NetworkManager::connected, [&] {
    netA.sendFrame(MsgType::Register, buildRegisterPayload(cryptoA, "alice_e2e", "password123"));
  });
  QObject::connect(&netB, &NetworkManager::connected, [&] {
    netB.sendFrame(MsgType::Register, buildRegisterPayload(cryptoB, "bob_e2e", "password123"));
  });

  QObject::connect(&netA, &NetworkManager::frameReceived, [&](MsgType type, Bytes payload) {
    switch (type) {
      case MsgType::RegisterOk:
        netA.sendFrame(MsgType::Login, buildLoginPayload("alice_e2e", "password123"));
        break;
      case MsgType::RegisterErr: {
        Reader r(payload);
        fail("alice register err: " + QString::fromStdString(r.str()));
        break;
      }
      case MsgType::LoginOk:
        aliceLoggedIn = true;
        std::cout << "[Alice] login OK\n";
        maybeStartConversation();
        break;
      case MsgType::LoginErr: {
        Reader r(payload);
        fail("alice login err: " + QString::fromStdString(r.str()));
        break;
      }
      case MsgType::PrekeyBundle: {
        Reader r(payload);
        PrekeyBundle bundle{};
        Bytes idX = r.blob();
        Bytes idEd = r.blob();
        Bytes spk = r.blob();
        bundle.signedPrekeySig = r.blob();
        std::copy(idX.begin(), idX.end(), bundle.identityPkX25519);
        std::copy(idEd.begin(), idEd.end(), bundle.identityPkEd25519);
        std::copy(spk.begin(), spk.end(), bundle.signedPrekeyPub);

        if (!bundle.verify()) {
          fail("el bundle de bob no verifica");
          break;
        }

        auto first = cryptoA.encryptFirst("bob_e2e", bundle, MessagePayload::encodeText("ronda-0-A"));
        Writer w;
        w.str("bob_e2e");
        w.u8(1);
        w.blob(first.senderIdentityPkX25519);
        w.blob(first.senderEphemeralPk);
        w.blob(first.ciphertext);
        w.blob(Bytes());  // usedOneTimePrekeyPub: este test no ejercita el pool de OTPK
        netA.sendFrame(MsgType::SendMsg, w.take());
        std::cout << "[Alice] ronda 0 enviada (con bootstrap X3DH)\n";
        break;
      }
      case MsgType::PrekeyBundleErr: {
        Reader r(payload);
        fail("prekey bundle err: " + QString::fromStdString(r.str()));
        break;
      }
      case MsgType::DeliverMsg: {
        Reader r(payload);
        uint32_t mailboxId = r.u32();
        std::string sender = r.str();
        r.u8();  // is_first: Alice solo espera respuestas (nunca primer mensaje)
        r.blob();
        r.blob();
        Bytes ct = r.blob();

        std::string text = MessagePayload::decode(cryptoA.decryptNext(sender, ct)).text;
        std::cout << "[Alice recibio de " << sender << "]: " << text << "\n";

        std::string expected = "ronda-" + std::to_string(roundsCompleted) + "-B";
        if (text != expected) {
          fail("Alice esperaba '" + QString::fromStdString(expected) + "' y recibio '" +
              QString::fromStdString(text) + "'");
          break;
        }
        roundsCompleted += 1;

        Writer ack;
        ack.u32(mailboxId);
        netA.sendFrame(MsgType::Ack, ack.take());

        if (roundsCompleted >= kTotalRounds) {
          QCoreApplication::exit(0);
        } else {
          std::string next = "ronda-" + std::to_string(roundsCompleted) + "-A";
          Bytes ciphertext = cryptoA.encryptNext(sender, MessagePayload::encodeText(next));
          Writer w;
          w.str(sender);
          w.u8(0);
          w.blob(Bytes());
          w.blob(Bytes());
          w.blob(ciphertext);
          w.blob(Bytes());
          netA.sendFrame(MsgType::SendMsg, w.take());
        }
        break;
      }
      default:
        break;
    }
  });

  QObject::connect(&netB, &NetworkManager::frameReceived, [&](MsgType type, Bytes payload) {
    switch (type) {
      case MsgType::RegisterOk:
        netB.sendFrame(MsgType::Login, buildLoginPayload("bob_e2e", "password123"));
        break;
      case MsgType::RegisterErr: {
        Reader r(payload);
        fail("bob register err: " + QString::fromStdString(r.str()));
        break;
      }
      case MsgType::LoginOk:
        bobLoggedIn = true;
        std::cout << "[Bob] login OK\n";
        maybeStartConversation();
        break;
      case MsgType::LoginErr: {
        Reader r(payload);
        fail("bob login err: " + QString::fromStdString(r.str()));
        break;
      }
      case MsgType::DeliverMsg: {
        Reader r(payload);
        uint32_t mailboxId = r.u32();
        std::string sender = r.str();
        bool isFirst = r.u8() != 0;
        Bytes idX = r.blob();
        Bytes eph = r.blob();
        Bytes ct = r.blob();

        Bytes decrypted = isFirst ? cryptoB.decryptFirst(sender, idX, eph, ct, /*usedOneTimePrekey=*/nullptr)
                                  : cryptoB.decryptNext(sender, ct);
        std::string text = MessagePayload::decode(decrypted).text;
        std::cout << "[Bob recibio de " << sender << "]: " << text << "\n";

        // Bob responde con la misma ronda pero sufijo "-B", reusando la
        // sesion ya establecida (para el primer mensaje, por decryptFirst;
        // para los siguientes, porque el ratchet ya avanzo en turnos
        // anteriores) -- nunca necesita su propio FetchPrekeyBundle.
        {
          std::string reply = text;
          if (reply.size() >= 1 && reply.back() == 'A') reply.back() = 'B';
          Bytes replyCt = cryptoB.encryptNext(sender, MessagePayload::encodeText(reply));
          Writer w;
          w.str(sender);
          w.u8(0);
          w.blob(Bytes());
          w.blob(Bytes());
          w.blob(replyCt);
          w.blob(Bytes());
          netB.sendFrame(MsgType::SendMsg, w.take());
          std::cout << "[Bob] respondio: " << reply << "\n";
        }

        Writer ack;
        ack.u32(mailboxId);
        netB.sendFrame(MsgType::Ack, ack.take());
        break;
      }
      default:
        break;
    }
  });

  QTimer::singleShot(5000, [&] { fail("timeout: la conversacion no se completo en 5s"); });

  netA.connectToServer("127.0.0.1", port);
  netB.connectToServer("127.0.0.1", port);

  int rc = app.exec();
  if (rc == 0) {
    std::cout << "[OK] Flujo end-to-end completo: registro, login, X3DH, " << kTotalRounds
              << " rondas de Double Ratchet bidireccional.\n";
  }
  return rc;
}
