# 06 -- Tests: que prueba cada uno y por que estan escritos asi

No hay un unico framework de testing "de verdad" (nada de GoogleTest ni
Catch2) -- cada test es un `main()` normal que hace comprobaciones con una
funcion `check(condicion, mensaje)` que lanza si falla, y que imprime
`[OK]`/`[FAIL]` por cada paso. Es deliberadamente simple: en un proyecto de
este tamaño, la libreria de testing no aporta tanto como para justificar la
dependencia extra.

## Los tres niveles

### 1. Cripto pura, sin red ni Qt

`crypto/tests/test_x3dh.cpp`, `crypto/tests/test_double_ratchet.cpp`.

Instancian `Identity`, llaman `x3dhInitiate`/`x3dhRespond`,
`DoubleRatchet::encrypt`/`decrypt` directamente, sin ningun socket ni
ventana de por medio. Son los mas rapidos (milisegundos) y los mas
faciles de razonar -- cada nombre de test es el escenario exacto que
reproduce: `x3dh_rejects_invalid_bundle`, `out_of_order_delivery`,
`tampered_ciphertext_rejected`, `max_skip_limit_enforced`,
`serialize_preserves_skipped_keys`... Si quieres entender un detalle fino
del Double Ratchet (ver [02-criptografia.md](02-criptografia.md)), el test
correspondiente suele ser mas claro que releer la implementacion.

```bash
./build/crypto/templar_crypto_tests
./build/crypto/templar_ratchet_tests
```

### 2. Cliente sin red real, o con red pero sin GUI

`client/tests/persistence_smoke.cpp`, `client/tests/migration_smoke.cpp`:
no necesitan ningun `templar_server` corriendo. Prueban `LocalStore`
directamente -- que una sesion sobreviva a "cerrar y reabrir la app"
(destruir y reconstruir los objetos, volver a desbloquear desde disco), y
que un almacen con el esquema viejo se migre sin perder datos.

`client/tests/e2e_smoke.cpp`: SI necesita un servidor real, pero usa
`NetworkManager`/`CryptoEngine` directamente, sin pasar por `MainWindow` ni
abrir ninguna ventana. Prueba el flujo completo de red+cripto (registro,
login, X3DH, varias rondas de Double Ratchet bidireccional) al nivel mas
bajo posible.

```bash
./build/server/templar_server 19999 /tmp/test.db &
./build/client/templar_e2e_smoke 19999
```

### 3. GUI real: los mismos widgets que usaria una persona

`client/tests/gui_smoke.cpp`, `client/tests/group_smoke.cpp`,
`client/tests/reregister_smoke.cpp`.

Estos instancian `MainWindow` de verdad (dos, tipicamente `alice` y `bob`)
y manejan los widgets reales: `find<QPushButton>(&alice, "sendButton")->click()`,
`find<QLineEdit>(&alice, "messageEdit")->setText(...)`. Corren bajo
`QT_QPA_PLATFORM=offscreen` (una plataforma de Qt que no dibuja en una
pantalla real, pero mantiene toda la semantica de widgets/eventos) para
poder correr en CI o en una terminal sin entorno grafico.

**¿Por que no basta con probar `NetworkManager`/`CryptoEngine` a bajo nivel
como hace `e2e_smoke`?** Porque hay bugs que solo existen en la capa de UI
-- por ejemplo, `MainWindow` decide cuando marcar una conversacion como "no
leida" o cuando persistir una sesion, y esa logica vive en `MainWindow.cpp`,
no en `CryptoEngine`. Un test que se salte esa capa nunca detectaria un bug
ahi.

Cada widget interactivo tiene un `setObjectName(...)` explicito
precisamente para esto (`sendButton_->setObjectName("sendButton")`,
`buildChatPage()`) -- es el "gancho" que usan los tests para encontrarlo
con `findChild<T*>(nombre)`.

### El patron: separar la accion real del dialogo modal

Un dialogo modal real (`QFileDialog`, `QMessageBox`) es notoriamente fragil
de automatizar -- intentar simular clics dentro de el desde un test
externo funciona a veces si y a veces no, dependiendo de detalles internos
de Qt. La solucion que se uso en este proyecto, cada vez que hizo falta:
separar "mostrar el dialogo y leer lo que eligio el usuario" de "que hacer
con esa eleccion", y dejar la segunda parte como un metodo invocable
directamente:

```cpp
// MainWindow.cpp
void MainWindow::onAttachClicked() {           // NO se prueba directamente
  QString path = QFileDialog::getOpenFileName(...);
  if (!path.isEmpty()) startOutgoingFileTransfer(path);
}
void MainWindow::startOutgoingFileTransfer(const QString& path) {  // esto SI
  // ... toda la logica real ...
}
```

```cpp
// gui_smoke.cpp
QMetaObject::invokeMethod(&alice, "startOutgoingFileTransfer", Q_ARG(QString, sourcePath));
```

`QMetaObject::invokeMethod` puede llamar slots privados por su nombre (bypasea
el control de acceso normal de C++) porque Qt los conoce via su sistema de
metaobjetos -- es lo que hace posible probar la logica real sin pelear con
el dialogo modal en absoluto. El mismo patron se aplica al panel de
invitaciones de grupo (`inviteList_`/`acceptInviteButton_`) que, al ser un
widget normal no-modal, ni siquiera necesita este truco -- se puede
interactuar con el directamente como con cualquier boton.

### Diseñar el tamaño de los datos de prueba a proposito

El test de transferencia de archivos en `gui_smoke.cpp` usa un archivo de
2 MB (~32 bloques de 64 KB), no uno pequeño. Hay un comentario explicando
por que: el bug real de la cola de escritura (ver
[03-servidor.md](03-servidor.md)) solo se manifestaba con una rafaga rapida
de mensajes seguidos -- un archivo de un solo bloque nunca la habria
detectado. Es una leccion general: el TAMAÑO de los datos de un test no es
un detalle arbitrario, a veces es la unica forma de que el test pueda
fallar cuando deberia.

## Correr todo junto

Los tests que necesitan red arrancan su propio `templar_server` de usar y
tirar, apuntando a un archivo de base de datos temporal (para no tocar tu
servidor real ni tus cuentas de verdad):

```bash
rm -f /tmp/templar_test.db
./build/server/templar_server 19999 /tmp/templar_test.db &
SERVER_PID=$!

./build/client/templar_e2e_smoke 19999
./build/client/templar_gui_smoke 19999
./build/client/templar_group_smoke 19999

kill $SERVER_PID
```

`reregister_smoke` simula especificamente el caso "el servidor se reseteo
(otra base de datos) pero mi cliente todavia tiene el almacen local viejo",
asi que necesita DOS servidores:

```bash
./build/server/templar_server 19998 /tmp/templar_test2.db &
./build/client/templar_reregister_smoke 19999 19998
```
