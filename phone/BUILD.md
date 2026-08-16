# Compilar el cliente movil (Android)

Esta guia cubre, en dos partes:

1. **Instalacion del entorno** (Qt para Android, Android SDK/NDK, JDK) -- se
   hace una vez por maquina, es sobre todo interactiva (instaladores
   graficos), asi que no esta pensada para automatizarse del todo.
2. **Compilar y generar el APK** -- una vez el entorno esta listo, esto SI
   esta automatizado por `build_mobile.sh` (en la raiz del proyecto). Esta
   guia explica que hace ese script paso a paso, para poder reproducirlo a
   mano o diagnosticar un fallo.

Para el cliente de **escritorio** (Linux, Qt Widgets) usa `setup.sh` en la
raiz del proyecto en su lugar -- ese si es un instalador de un solo paso,
porque sus dependencias vienen todas de los repositorios de la distro.

---

## 1. Instalar el entorno (una sola vez)

### 1.1. Qt para Android

Se necesita el **Qt Online Installer** (cuenta gratuita de Qt Open Source
suficiente): <https://www.qt.io/download-qt-installer>

Al elegir componentes, dentro de la version de Qt que instales (este
proyecto se ha compilado y probado con **Qt 6.11.1**, pero cualquier 6.5+
deberia servir -- `qt_standard_project_setup(REQUIRES 6.5)` en el
CMakeLists.txt raiz es el minimo real) marca:

- **Android para arm64-v8a** (el kit de compilacion cruzada en si)
- El kit de **escritorio para Linux** correspondiente (p.ej. "Desktop gcc
  64-bit") -- hace falta como `QT_HOST_PATH`, Qt para Android necesita un Qt
  de escritorio ya compilado para generar codigo QML en tiempo de
  compilacion (`qmltyperegistrations`, etc.)
- **Android OpenSSL** (bajo "Additional Libraries" o similar, segun la
  version del instalador) -- son los `.so` de OpenSSL de KDAB
  (<https://github.com/KDAB/android_openssl>) que Qt para Android NO trae
  por si solo. Sin esto la app compila pero **crashea al abrir** (el plugin
  TLS de Qt hace `dlopen()` de libssl/libcrypto y no las encuentra). Si tu
  version del instalador no lo ofrece como componente, clona ese repo a
  mano en `<Android Sdk>/android_openssl/`.

Instalacion tipica: `~/Qt/<version>/android_arm64_v8a/` (kit Android) y
`~/Qt/<version>/gcc_64/` (kit de escritorio, para `QT_HOST_PATH`).

### 1.2. Android SDK + NDK

El Qt Online Installer puede instalar Android Studio/el SDK por ti, o
puedes usar uno que ya tengas. Este proyecto se ha probado con:

- **NDK 27.2.12479018** (Qt para Android suele fijar una version concreta
  de NDK compatible con esa version de Qt -- revisa la documentacion de Qt
  si usas una version de Qt distinta)
- **Android SDK** con la plataforma **android-34** o superior instalada
  (el proyecto compila contra `QT_ANDROID_TARGET_SDK_VERSION 34`, ver el
  comentario en `phone/CMakeLists.txt` sobre por que no se sube a 35+
  todavia: el modo edge-to-edge obligatorio de la API 35 necesita gestion
  de insets que aun no esta implementada)

Ubicacion tipica: `~/Android/Sdk`.

### 1.3. JDK -- **tiene que ser la version 17**

El Android Gradle Plugin (usado por Qt para empaquetar el APK) no funciona
con JDK muy nuevo (probado: **JDK 26 rompe el paso de empaquetado** con un
error de `jlink`/`core-for-system-modules.jar` al compilar
`compileDebugJavaWithJavac`). Si tu sistema tiene un JDK mas nuevo como
version por defecto (`java -version`), instala ademas el 17:

```bash
# Arch
sudo pacman -S jdk17-openjdk

# Debian/Ubuntu
sudo apt-get install openjdk-17-jdk
```

No hace falta cambiar el JDK por defecto del sistema -- `build_mobile.sh`
fija `JAVA_HOME` solo para el paso de empaquetado, sin tocar nada global.
Si compilas a mano, exporta `JAVA_HOME=/usr/lib/jvm/java-17-openjdk` (la
ruta exacta puede variar segun la distro -- `archlinux-java status` o
`update-alternatives --list java` para verla) antes del paso de
empaquetado.

### 1.4. Dispositivo o emulador para probar

Para instalar y probar en un movil real: activa "Opciones de
desarrollador" -> "Depuracion USB" en el telefono, conectalo por USB, y
autoriza el ordenador cuando el telefono lo pida. `adb devices` (dentro de
`<Android Sdk>/platform-tools/`) deberia listarlo como `device` (no
`unauthorized`).

---

## 2. Compilar

### 2.1. Build "de escritorio simulado" (iteracion rapida)

Antes de meterte con el empaquetado de Android (mas lento), puedes
compilar y ejecutar la UI de Qt Quick del movil como un binario de
escritorio normal -- mismo codigo QML/C++, corre en la maquina de
desarrollo sin pasar por Android en absoluto. Sirve para iterar rapido en
UI/logica antes de probar en el dispositivo real:

```bash
cmake -S . -B build-phone -DCMAKE_BUILD_TYPE=Debug -DTEMPLAR_BUILD_PHONE=ON
cmake --build build-phone --target templar_phone -j$(nproc)
./build-phone/phone/templar_phone
```

### 2.2. Build real de Android + generar el APK

Esto es lo que automatiza `build_mobile.sh` (ver la seccion 3). A mano,
son estos pasos:

**Configurar** (una vez, o cuando cambies algo del CMakeLists.txt):

```bash
QT_VERSION=6.11.1   # ajusta a la version que hayas instalado
NDK_VERSION=27.2.12479018   # ajusta a la version que tengas en <SDK>/ndk/

cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/Qt/$QT_VERSION/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_HOST_PATH="$HOME/Qt/$QT_VERSION/gcc_64" \
  -DANDROID_SDK_ROOT="$HOME/Android/Sdk" \
  -DANDROID_NDK_ROOT="$HOME/Android/Sdk/ndk/$NDK_VERSION" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTEMPLAR_BUILD_PHONE=ON
```

**Compilar la libreria nativa + generar el APK** (el target
`templar_phone_make_apk` lo crea automaticamente el CMake de Qt6 para
cualquier ejecutable Android -- no hay que definirlo a mano en
`phone/CMakeLists.txt`):

```bash
JAVA_HOME=/usr/lib/jvm/java-17-openjdk \
  cmake --build build-android --target templar_phone_make_apk -j$(nproc)
```

`CMAKE_BUILD_TYPE=Debug` es lo que hace que salga un APK de **depuracion**
(firmado automaticamente con la clave de depuracion, instalable
directamente con `adb install`) en vez de uno de **release sin firmar**
(que necesitaria firmarlo a mano antes de poder instalarlo en cualquier
sitio) -- si cambias a `Release`/`RelWithDebInfo`, el APK aparece en
`.../apk/release/android-build-release-unsigned.apk` en su lugar.

El APK de depuracion queda en:

```
build-android/phone/android-build/build/outputs/apk/debug/android-build-debug.apk
```

**Instalar en un dispositivo conectado:**

```bash
~/Android/Sdk/platform-tools/adb install -r \
  build-android/phone/android-build/build/outputs/apk/debug/android-build-debug.apk
```

`-r` reinstala/actualiza si ya estaba instalada (conserva los datos de la
app -- la cuenta y el historial local NO se pierden al actualizar).

### 2.3. Problemas frecuentes

- **La app crashea al abrir, nada mas lanzarla**: casi siempre faltan
  `libssl_3.so`/`libcrypto_3.so` (ver 1.1, componente "Android OpenSSL").
  El propio CMake avisa de esto con un `message(WARNING ...)` en tiempo de
  configuracion si no las encuentra -- revisa la salida de `cmake -S ...`.
- **Fallo de `compileDebugJavaWithJavac`/`jlink`/`core-for-system-modules`**:
  JDK por defecto demasiado nuevo -- ver 1.3, hace falta JDK 17 via
  `JAVA_HOME` para el paso de empaquetado especificamente.
- **"Module 'Templar' contains no type named 'Main'" en tiempo de
  ejecucion pese a que el .so enlaza bien**: falta la llamada a
  `qt_standard_project_setup(REQUIRES 6.5)` en el CMakeLists.txt -- ya esta
  puesta en este proyecto, pero si alguna vez se quita sin querer al
  fusionar cambios, este es el sintoma.
- **`adb devices` muestra `unauthorized`**: revisa el telefono, deberia
  haber un dialogo pidiendo autorizar este ordenador para depuracion USB
  (a veces queda detras de otra ventana/notificacion).
