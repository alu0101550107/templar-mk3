#pragma once

#include <QString>

namespace templar::phone {

// Vuelca lineas con hora a un archivo fijo (ver DebugLog.cpp para la ruta
// exacta) -- SOLO si el build se configuro con -DTEMPLAR_DEBUG_LOGGING=ON
// (ver phone/CMakeLists.txt); en un build normal esta funcion no hace nada,
// coste cero, para poder dejar las llamadas a debugLog(...) permanentemente
// en el codigo sin que afecten al build de verdad que se publica.
//
// Pensado para un caso muy concreto: un fallo que solo pasa en un
// dispositivo remoto (un amigo/beta-tester) que no podemos depurar en
// directo -- se genera un APK aparte con esta opcion activada, la persona
// reproduce el fallo, y nos manda el archivo (la ruta se anuncia sola por
// el chat "Sistema" al arrancar, ver ClientController::announceDebugLogPath).
void debugLog(const QString& message);

// Ruta absoluta del archivo de arriba -- cadena vacia si el build no tiene
// TEMPLAR_DEBUG_LOGGING activado. ClientController la usa para anunciarla
// por el chat "Sistema" al arrancar (ver announceDebugLogPath).
QString debugLogFilePath();

}  // namespace templar::phone
