#include "BiometricBridge.hpp"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
#ifdef Q_OS_ANDROID
// El callback de Java (BiometricHelper.authenticate()) llega en el hilo del
// Executor de Android, no en el hilo de Qt -- este puntero y las funciones
// JNI de mas abajo (a nivel global, fuera de cualquier namespace, porque
// JNI busca las funciones por su nombre completo sin margen para
// namespaces de C++) encuentran la instancia viva para reenviarle el
// resultado via una senal en cola.
templar::phone::BiometricBridge* g_instance = nullptr;

constexpr char kHelperClass[] = "com/templar/phone/BiometricHelper";
#endif
}  // namespace

namespace templar::phone {

BiometricBridge::BiometricBridge(QObject* parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
  g_instance = this;
  connect(this, &BiometricBridge::enableResultReady, this, &BiometricBridge::handleEnableResult,
          Qt::QueuedConnection);
  connect(this, &BiometricBridge::unlockResultReady, this, &BiometricBridge::handleUnlockResult,
          Qt::QueuedConnection);
#endif
}

bool BiometricBridge::available() const {
#ifdef Q_OS_ANDROID
  return QJniObject::callStaticMethod<jboolean>(
      kHelperClass, "isAvailable", "(Landroid/content/Context;)Z",
      QNativeInterface::QAndroidApplication::context());
#else
  return false;
#endif
}

bool BiometricBridge::enabled() const {
#ifdef Q_OS_ANDROID
  return QJniObject::callStaticMethod<jboolean>(
      kHelperClass, "isEnabled", "(Landroid/content/Context;)Z",
      QNativeInterface::QAndroidApplication::context());
#else
  return false;
#endif
}

void BiometricBridge::enable(const QString& password) {
#ifdef Q_OS_ANDROID
  QJniObject jPassword = QJniObject::fromString(password);
  QJniObject::callStaticMethod<void>(kHelperClass, "enable",
                                      "(Landroid/content/Context;Ljava/lang/String;)V",
                                      QNativeInterface::QAndroidApplication::context(),
                                      jPassword.object());
#else
  Q_UNUSED(password);
  emit enableFinished(false, tr("No disponible fuera de Android"));
#endif
}

void BiometricBridge::unlock() {
#ifdef Q_OS_ANDROID
  QJniObject::callStaticMethod<void>(kHelperClass, "unlock", "(Landroid/content/Context;)V",
                                      QNativeInterface::QAndroidApplication::context());
#else
  emit unlockFinished(false, QString(), tr("No disponible fuera de Android"));
#endif
}

void BiometricBridge::disable() {
#ifdef Q_OS_ANDROID
  QJniObject::callStaticMethod<void>(kHelperClass, "disable", "(Landroid/content/Context;)V",
                                      QNativeInterface::QAndroidApplication::context());
  emit enabledChanged();
#endif
}

void BiometricBridge::handleEnableResult(bool success, QString errorMessage) {
  emit enabledChanged();
  emit enableFinished(success, errorMessage);
}

void BiometricBridge::handleUnlockResult(bool success, QString password, QString errorMessage) {
  emit unlockFinished(success, password, errorMessage);
}

}  // namespace templar::phone

#ifdef Q_OS_ANDROID
extern "C" JNIEXPORT void JNICALL Java_com_templar_phone_BiometricHelper_nativeOnEnableResult(
    JNIEnv* env, jclass, jboolean success, jstring errorMessage) {
  Q_UNUSED(env);
  QString error = errorMessage ? QJniObject(errorMessage).toString() : QString();
  if (g_instance) {
    emit g_instance->enableResultReady(success, error);
  }
}

extern "C" JNIEXPORT void JNICALL Java_com_templar_phone_BiometricHelper_nativeOnUnlockResult(
    JNIEnv* env, jclass, jboolean success, jstring password, jstring errorMessage) {
  Q_UNUSED(env);
  QString pass = password ? QJniObject(password).toString() : QString();
  QString error = errorMessage ? QJniObject(errorMessage).toString() : QString();
  if (g_instance) {
    emit g_instance->unlockResultReady(success, pass, error);
  }
}
#endif
