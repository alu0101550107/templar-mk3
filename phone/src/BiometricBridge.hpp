#pragma once

#include <QObject>
#include <QString>

namespace templar::phone {

// Puente entre QML y BiometricHelper.java (ver ese archivo para el porque
// de usar la BiometricPrompt de plataforma en vez de androidx.biometric, y
// para el porque de invertir a proposito la decision de "nunca guardar la
// contraseña" solo para este caso). Fuera de Android, available()/
// enabled() son siempre false y enable()/unlock() fallan de inmediato --
// esto es exclusivamente para el movil.
class BiometricBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)

 public:
  explicit BiometricBridge(QObject* parent = nullptr);

  bool available() const;
  bool enabled() const;

  // Pide la huella y, si se confirma, cifra password con una clave del
  // Keystore protegida por biometria. El resultado llega por
  // enableFinished (asincrono: el dialogo del sistema no bloquea).
  Q_INVOKABLE void enable(const QString& password);

  // Pide la huella y, si se confirma, descifra la contraseña guardada. El
  // resultado llega por unlockFinished -- si success es true, password
  // trae la contraseña en claro lista para pasarsela a
  // ClientController::login() junto con rememberedUsername().
  Q_INVOKABLE void unlock();

  // Olvida la contraseña guardada y borra la clave del Keystore.
  Q_INVOKABLE void disable();

 signals:
  void enabledChanged();
  void enableFinished(bool success, const QString& errorMessage);
  void unlockFinished(bool success, const QString& password, const QString& errorMessage);

  // Solo para uso interno (ver BiometricBridge.cpp): reenvian el
  // resultado desde el hilo en el que corre el callback de Java hacia el
  // hilo de Qt, via una conexion en cola.
  void enableResultReady(bool success, QString errorMessage);
  void unlockResultReady(bool success, QString password, QString errorMessage);

 private slots:
  void handleEnableResult(bool success, QString errorMessage);
  void handleUnlockResult(bool success, QString password, QString errorMessage);
};

}  // namespace templar::phone
