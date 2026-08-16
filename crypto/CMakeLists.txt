add_library(templar_crypto STATIC
  src/Identity.cpp
  src/X3DH.cpp
  src/DoubleRatchet.cpp
  src/FileCrypto.cpp
)
target_include_directories(templar_crypto PUBLIC include)

if(ANDROID)
  # No hay libsodium del sistema para Android (ni pkg-config apunta a nada
  # util cuando se cross-compila) -- se usa un libsodium precompilado con
  # los scripts oficiales de dist-build/ (NDK r27c, API 28, build
  # "completa" no minimal) para cada ABI que se necesite. Solo arm64-v8a
  # por ahora (la ABI de cualquier movil Android actual); anadir mas
  # subcarpetas aqui si algun dia hace falta armv7/x86_64 (p.ej. para el
  # emulador).
  set(_sodium_root "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libsodium-android/${CMAKE_ANDROID_ARCH_ABI}")
  if(NOT EXISTS "${_sodium_root}/lib/libsodium.a")
    message(FATAL_ERROR
      "No se encuentra libsodium precompilado para ${CMAKE_ANDROID_ARCH_ABI} en "
      "${_sodium_root} -- compilarlo con dist-build/android-*.sh de libsodium "
      "(ver third_party/libsodium-android/README si existe, o el historial de "
      "como se genero arm64-v8a).")
  endif()
  add_library(templar_sodium STATIC IMPORTED)
  set_target_properties(templar_sodium PROPERTIES
    IMPORTED_LOCATION "${_sodium_root}/lib/libsodium.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_sodium_root}/include"
  )
  target_link_libraries(templar_crypto PUBLIC templar_sodium)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  target_link_libraries(templar_crypto PUBLIC PkgConfig::SODIUM)
endif()

# Los tests de crypto/ compilan como ejecutables normales de escritorio --
# cross-compilados para Android no se podrian correr en este host (ni hay
# ctest en un movil), y no aportan nada al build del cliente movil en si.
if(NOT ANDROID)
  enable_testing()

  add_executable(templar_crypto_tests
    tests/test_x3dh.cpp
  )
  target_link_libraries(templar_crypto_tests PRIVATE templar_crypto)
  add_test(NAME crypto_tests COMMAND templar_crypto_tests)

  add_executable(templar_ratchet_tests
    tests/test_double_ratchet.cpp
  )
  target_link_libraries(templar_ratchet_tests PRIVATE templar_crypto)
  add_test(NAME ratchet_tests COMMAND templar_ratchet_tests)
endif()
