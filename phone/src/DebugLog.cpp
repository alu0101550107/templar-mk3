#include "DebugLog.hpp"

#ifdef TEMPLAR_DEBUG_LOGGING
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#endif

namespace templar::phone {

#ifdef TEMPLAR_DEBUG_LOGGING

namespace {

// Carpeta propia de la app en almacenamiento externo (equivalente Java:
// getExternalFilesDir(null)) -- no necesita ningun permiso en ninguna
// version de Android, a diferencia de escribir en Descargas de verdad
// (eso ya se resolvio para archivos reales via MediaStore, ver
// MediaStoreHelper.java; aqui no compensa esa ceremonia para un simple
// archivo de texto que se usa una vez y se tira). Visible con cualquier
// gestor de archivos que muestre almacenamiento interno/Android/data.
QString logFilePath() {
  return QStringLiteral(
      "/storage/emulated/0/Android/data/com.templar.phone/files/templar_debug.log");
}

QMutex& logMutex() {
  static QMutex mutex;
  return mutex;
}

}  // namespace

QString debugLogFilePath() { return logFilePath(); }

void debugLog(const QString& message) {
  QMutexLocker locker(&logMutex());
  QFile file(logFilePath());
  if (!file.open(QIODevice::Append | QIODevice::Text)) return;
  QTextStream out(&file);
  out << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' ' << message
      << '\n';
}

#else

QString debugLogFilePath() { return QString(); }

void debugLog(const QString&) {}

#endif

}  // namespace templar::phone
