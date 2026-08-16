# Cliente movil (Qt Quick/QML). ClientController (src/ClientController.cpp)
# reusa TAL CUAL NetworkManager/CryptoEngine/LocalStore de client/src/ --
# ninguna de las tres depende de Qt Widgets, asi que sirven sin adaptar nada
# como backend de red/cripto/persistencia para las paginas QML.
#
# A diferencia de client/ y server/ (que cogen sqlite3 del sistema via
# pkg-config), aqui se enlaza contra third_party/sqlite3 -- Android no tiene
# un libsqlite3.so publico estable, asi que hace falta llevarlo vendorizado.
# Se usa el mismo target vendorizado tambien en el build de escritorio (en
# vez de tener dos rutas de compilacion distintas) para que lo que se prueba
# aqui sea exactamente lo que se compilaria para el movil de verdad.

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Network)

# Sin esta llamada, qt6_add_qml_module no deja el modulo QML en un estado
# que Android pueda resolver en tiempo de ejecucion (fallaba con "Module
# 'Templar' contains no type named 'Main'" pese a que el .so se enlazaba
# bien) -- se detecto por bisection contra un proyecto de prueba identico
# que si funcionaba y que si llamaba a esta macro.
qt_standard_project_setup(REQUIRES 6.5)

if(POLICY QTP0001)
  qt_policy(SET QTP0001 NEW)
endif()

qt6_add_executable(templar_phone
  src/main.cpp
  src/ClientController.cpp
  src/ThemeController.cpp
  src/ConversationListModel.cpp
  src/ChatHistoryModel.cpp
  ${CMAKE_SOURCE_DIR}/client/src/NetworkManager.cpp
  ${CMAKE_SOURCE_DIR}/client/src/CryptoEngine.cpp
  ${CMAKE_SOURCE_DIR}/client/src/LocalStore.cpp
  ${CMAKE_SOURCE_DIR}/client/src/MessagePayload.cpp
  ${CMAKE_SOURCE_DIR}/client/src/Theme.cpp
)
target_include_directories(templar_phone PRIVATE ${CMAKE_SOURCE_DIR}/client/src)

if(ANDROID)
  # Manifest propio (android/AndroidManifest.xml) en vez del que genera Qt
  # por defecto -- solo para fijar el package id y el nombre visible de la
  # app ("Templar" en vez del generico "org.qtproject.example.templar_phone").
  set_target_properties(templar_phone PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    # SDK 35+ obliga al modo "edge-to-edge" (contenido bajo la barra de
    # estado/navegacion) sin forma de desactivarlo -- sin gestionar los
    # insets a mano esto deja una franja del color por defecto del sistema
    # (blanca) sin cubrir arriba. Bajado hasta que el manejo de insets se
    # implemente como caracteristica real en vez de dejarlo como efecto
    # secundario no gestionado.
    QT_ANDROID_TARGET_SDK_VERSION 34
  )

  # NetworkManager usa QSslSocket (TLS) -- el backend TLS de Qt para
  # Android (libplugins_tls_qopensslbackend_*.so, que SI se empaqueta
  # solo) necesita libssl/libcrypto en tiempo de ejecucion, y esas NO se
  # empaquetan solas: hay que apuntarlas explicitamente via
  # QT_ANDROID_EXTRA_LIBS. Sin esto, el plugin TLS no puede cargarse -- y
  # un fallo de dlopen() de un plugin durante la inicializacion temprana
  # de Qt es la causa mas comun documentada del crash "FORTIFY:
  # pthread_mutex_lock called on a destroyed mutex" en el hilo hwuiTask
  # que se ve en el movil real (la app se cierra sola nada mas abrirse).
  # Vienen precompiladas con el propio SDK de Android (repo
  # https://github.com/KDAB/android_openssl que instala el Qt Online
  # Installer bajo Android/Sdk/android_openssl).
  set(_openssl_root "$ENV{ANDROID_SDK_ROOT}/android_openssl/ssl_3/${CMAKE_ANDROID_ARCH_ABI}")
  if(NOT _openssl_root OR NOT EXISTS "${_openssl_root}/libssl_3.so")
    set(_openssl_root "${ANDROID_SDK_ROOT}/android_openssl/ssl_3/${CMAKE_ANDROID_ARCH_ABI}")
  endif()
  if(EXISTS "${_openssl_root}/libssl_3.so")
    set_property(TARGET templar_phone APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
      "${_openssl_root}/libssl_3.so"
      "${_openssl_root}/libcrypto_3.so"
    )
  else()
    message(WARNING
      "No se encontro android_openssl para ${CMAKE_ANDROID_ARCH_ABI} en "
      "${_openssl_root} -- la app probablemente crasheara al iniciar "
      "(plugin TLS sin libssl/libcrypto). Instalalo desde el Qt "
      "Maintenance Tool (componente 'Android OpenSSL') o clona "
      "https://github.com/KDAB/android_openssl en Android/Sdk/android_openssl.")
  endif()
endif()

qt6_add_qml_module(templar_phone
  URI Templar
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/LoginPage.qml
    qml/ChatListPage.qml
    qml/ChatPage.qml
    qml/TemplarBackground.qml
    qml/TemplarTextField.qml
    qml/TemplarButton.qml
    qml/TemplarToolBar.qml
    qml/TemplarItemDelegate.qml
    qml/TemplarSlider.qml
    qml/TemplarColorPicker.qml
    qml/SettingsDialog.qml
    qml/NewChatDialog.qml
    qml/CreateGroupDialog.qml
    qml/AddMemberDialog.qml
    qml/KickMemberDialog.qml
    qml/EmojiPicker.qml
  RESOURCES
    # Copias propias de los assets del escritorio (client/resources/): cada
    # modulo QML gestiona sus propios recursos, no hay una forma limpia de
    # compartir un .qrc entre dos targets de CMake distintos sin liarla mas
    # de lo que merece la pena. great_helmet_logo_blurred.png es el mismo
    # logo pre-desenfocado (ImageMagick, radio ~20px) en vez de desenfocado
    # en tiempo real via shader -- mismo resultado visual, sin depender de
    # que MultiEffect renderice bien en cada dispositivo.
    qml/assets/great_helmet_logo.png
    qml/assets/great_helmet_logo_blurred.png
)

target_link_libraries(templar_phone PRIVATE
  templar_common
  templar_crypto
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::QuickDialogs2
  Qt6::Network
  templar_sqlite3
)
