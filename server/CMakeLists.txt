find_package(Threads REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_executable(templar_server
  src/main.cpp
  src/Database.cpp
  src/BlobStore.cpp
  src/Session.cpp
  src/Router.cpp
)

target_link_libraries(templar_server PRIVATE
  templar_common
  templar_crypto
  Boost::boost
  Threads::Threads
  PkgConfig::SQLITE3
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_compile_definitions(templar_server PRIVATE BOOST_ASIO_NO_DEPRECATED)
