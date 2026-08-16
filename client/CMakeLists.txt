set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_executable(templar_client
  src/main.cpp
  src/NetworkManager.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
  src/MainWindow.cpp
  src/EmojiPicker.cpp
  src/BackgroundWidget.cpp
  src/LocalStore.cpp
  src/Theme.cpp
  src/SettingsDialog.cpp
  src/CreateGroupDialog.cpp
  resources/templar.qrc
)

target_link_libraries(templar_client PRIVATE
  templar_common
  templar_crypto
  Qt6::Widgets
  Qt6::Network
  PkgConfig::SQLITE3
)

# Prueba de integracion de extremo a extremo (Fase 1): requiere un
# templar_server real corriendo, ver client/tests/e2e_smoke.cpp.
add_executable(templar_e2e_smoke
  tests/e2e_smoke.cpp
  src/NetworkManager.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
)
target_include_directories(templar_e2e_smoke PRIVATE src)
target_link_libraries(templar_e2e_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Core
  Qt6::Network
)

# Prueba de que identidad/sesiones/historial sobreviven a cerrar y reabrir
# el cliente -- ver client/tests/persistence_smoke.cpp. No necesita red.
add_executable(templar_persistence_smoke
  tests/persistence_smoke.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
  src/LocalStore.cpp
)
target_include_directories(templar_persistence_smoke PRIVATE src)
target_link_libraries(templar_persistence_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Core
  PkgConfig::SQLITE3
)

# Prueba de que un almacen local con el esquema viejo de `history` (anterior
# a la Fase 4) se migra correctamente al desbloquearlo -- ver
# client/tests/migration_smoke.cpp. No necesita red.
add_executable(templar_migration_smoke
  tests/migration_smoke.cpp
  src/LocalStore.cpp
)
target_include_directories(templar_migration_smoke PRIVATE src)
target_link_libraries(templar_migration_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Core
  PkgConfig::SQLITE3
)

# Prueba que maneja dos MainWindow reales (los mismos widgets/slots que usa
# una persona) contra un servidor real -- a diferencia de e2e_smoke.cpp, no
# se salta la capa de UI. Ver client/tests/gui_smoke.cpp.
add_executable(templar_gui_smoke
  tests/gui_smoke.cpp
  src/NetworkManager.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
  src/MainWindow.cpp
  src/EmojiPicker.cpp
  src/BackgroundWidget.cpp
  src/LocalStore.cpp
  src/Theme.cpp
  src/SettingsDialog.cpp
  src/CreateGroupDialog.cpp
  resources/templar.qrc
)
target_include_directories(templar_gui_smoke PRIVATE src)
target_link_libraries(templar_gui_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Widgets
  Qt6::Network
  Qt6::Test
  PkgConfig::SQLITE3
)

# Reproduce el bug de re-registro con almacen local viejo (identidad
# restaurada en login sin coincidir con la recien registrada) -- ver
# client/tests/reregister_smoke.cpp. Requiere DOS templar_server.
add_executable(templar_reregister_smoke
  tests/reregister_smoke.cpp
  src/NetworkManager.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
  src/MainWindow.cpp
  src/EmojiPicker.cpp
  src/BackgroundWidget.cpp
  src/LocalStore.cpp
  src/Theme.cpp
  src/SettingsDialog.cpp
  src/CreateGroupDialog.cpp
  resources/templar.qrc
)
target_include_directories(templar_reregister_smoke PRIVATE src)
target_link_libraries(templar_reregister_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Widgets
  Qt6::Network
  Qt6::Test
  PkgConfig::SQLITE3
)

# Flujo completo de grupos via GUI real: crear, invitar, aceptar, mensaje de
# grupo bidireccional (fan-out sobre los canales 1-a-1 existentes), y
# expulsion -- ver client/tests/group_smoke.cpp.
add_executable(templar_group_smoke
  tests/group_smoke.cpp
  src/NetworkManager.cpp
  src/CryptoEngine.cpp
  src/MessagePayload.cpp
  src/MainWindow.cpp
  src/EmojiPicker.cpp
  src/BackgroundWidget.cpp
  src/LocalStore.cpp
  src/Theme.cpp
  src/SettingsDialog.cpp
  src/CreateGroupDialog.cpp
  resources/templar.qrc
)
target_include_directories(templar_group_smoke PRIVATE src)
target_link_libraries(templar_group_smoke PRIVATE
  templar_common
  templar_crypto
  Qt6::Widgets
  Qt6::Network
  Qt6::Test
  PkgConfig::SQLITE3
)
