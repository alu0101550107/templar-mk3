# SQLite vendorizado (amalgamation oficial, sqlite.org/download.html,
# version 3.53.4) en vez de enlazar contra el sqlite3 del sistema via
# pkg-config -- necesario para Android, donde no hay un libsqlite3.so
# publico estable con el que enlazar (el que trae el propio SO es privado
# desde API 30), y de paso deja identico el mismo binario de sqlite en
# escritorio y movil en vez de dos rutas de compilacion distintas.
#
# SQLITE_THREADSAFE=1: LocalStore abre/cierra la conexion en el hilo de UI
# nada mas, pero el modo "serialized" no cuesta nada aqui y evita sorpresas
# si en el futuro se toca desde otro hilo.
# SQLITE_OMIT_LOAD_EXTENSION: LocalStore no carga extensiones .so en
# tiempo de ejecucion -- quitar esa superficie de ataque no cuesta nada.
add_library(templar_sqlite3 STATIC sqlite3.c)
target_include_directories(templar_sqlite3 PUBLIC .)
target_compile_definitions(templar_sqlite3 PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_OMIT_LOAD_EXTENSION
)
