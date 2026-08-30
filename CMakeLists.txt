cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
# Fuente unica de verdad de la version -- se compila como constante en los
# clientes (ver client/src/UpdateChecker.cpp) para compararla contra el
# ultimo release publicado en GitHub. Subir esto en un commit propio junto
# con un tag "vX.Y.Z" y un Release de GitHub es lo que activa el aviso de
# "hay una version nueva" en quien todavia tenga la version anterior.
project(templar_mk3 VERSION 1.0.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# Apagado por defecto para no obligar a nadie que solo quiera compilar el
# cliente de escritorio (p.ej. un amigo usando setup.sh) a tener instalado
# Qt Quick/QML -- se activa a mano con -DTEMPLAR_BUILD_PHONE=ON mientras se
# desarrolla el cliente movil en phone/.
option(TEMPLAR_BUILD_PHONE "Compilar el esqueleto experimental de cliente movil (Qt Quick)" OFF)
# Apagado por defecto por el mismo motivo que TEMPLAR_BUILD_PHONE, pero al
# reves: para un despliegue de SOLO SERVIDOR (maquina sin entorno grafico,
# ver deploy_server.sh) no tiene sentido obligar a instalar Qt6 solo para
# que el paso de configure de CMake no falle -- el servidor en si
# (server/, Boost.Asio+OpenSSL) no depende de Qt en absoluto.
option(TEMPLAR_SERVER_ONLY "Omitir el cliente de escritorio (Qt) al configurar, para no necesitar Qt6 instalado" OFF)

add_subdirectory(common)
add_subdirectory(crypto)

# server/ (Boost.Asio+OpenSSL) y client/ (Qt Widgets) son solo de
# escritorio -- ninguno de los dos esta pensado para cross-compilarse, y
# ademas casi seguro que fallarian al configurar contra un sysroot de
# Android (pkg-config, Boost, etc. no existen ahi). Un build para Android
# solo tiene sentido para phone/.
if(NOT ANDROID)
  add_subdirectory(server)
  if(NOT TEMPLAR_SERVER_ONLY)
    add_subdirectory(client)
  endif()
endif()

if(TEMPLAR_BUILD_PHONE)
  add_subdirectory(third_party/sqlite3)
  add_subdirectory(phone)
endif()
