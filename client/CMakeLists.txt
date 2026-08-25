set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)
# Opcional a proposito (QUIET, no REQUIRED): lupdate/lrelease (paquete
# qt6-tools en Arch, qt6-l10n-tools/qttools5-dev-tools en Debian) no hace
# falta para compilar el cliente en si -- solo para regenerar/compilar las
# traducciones (ver el bloque "i18n" mas abajo). Sin el paquete instalado, el
# cliente compila igual, solo se queda sin mas idioma que el espanol (el
# propio del codigo fuente, ver Language.hpp).
find_package(Qt6 QUIET COMPONENTS LinguistTools)

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
  src/Language.cpp
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

# i18n: el .ts se versiona en el repo (i18n/templar_client_en.ts) y se
# regenera a mano con `cmake --build <build> --target templar_client_lupdate`
# cuando se anaden/cambian strings en tr() -- no en cada build normal, para
# no reescribir el archivo (y perder traducciones ya hechas) solo por
# compilar. lrelease (compilar .ts -> .qm) SI corre en cada build normal,
# igual que compilar cualquier otra fuente -- ver qt6_add_lrelease abajo.
if(Qt6LinguistTools_FOUND)
  set(TEMPLAR_CLIENT_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/templar_client_en.ts)
  qt6_add_lupdate(templar_client TS_FILES ${TEMPLAR_CLIENT_TS_FILES}
    LUPDATE_TARGET templar_client_lupdate
    # Sin esto, lupdate a veces no infiere bien el idioma objetivo (ingles)
    # a partir del sufijo del nombre de archivo -- fijarlo explicito evita
    # que las reglas de plural (%n) salgan mal (una sola forma en vez de
    # singular/plural).
    OPTIONS -target-language en)
  qt6_add_lrelease(templar_client
    TS_FILES ${TEMPLAR_CLIENT_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE TEMPLAR_CLIENT_QM_FILES
  )
  qt6_add_resources(templar_client "templar_client_i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${TEMPLAR_CLIENT_QM_FILES}
  )
else()
  message(WARNING
    "Qt6 LinguistTools no encontrado -- templar_client se compilara sin la "
    "traduccion al ingles embebida (quedara solo en espanol). Instala "
    "qt6-tools (Arch) o qt6-l10n-tools/qttools5-dev-tools (Debian) y "
    "reconfigura para incluirla.")
endif()

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
  src/Language.cpp
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
  src/Language.cpp
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
  src/Language.cpp
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
