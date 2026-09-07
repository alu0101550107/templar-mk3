package com.templar.phone;

import android.content.Intent;

// Subclase minima de QtActivity, solo para poder interceptar
// onActivityResult -- QtActivityBase (la clase real detras de QtActivity)
// no expone ningun punto de extension para registrar oyentes de resultado
// de actividad. No cambia nada del comportamiento normal de Qt: deja que
// QtActivity procese el resultado primero (super.onActivityResult) y
// simplemente reenvia el aviso a CameraHelper despues.
public class TemplarActivity extends org.qtproject.qt.android.bindings.QtActivity {
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        CameraHelper.onActivityResult(requestCode, resultCode);
    }

    // Sin esto: al deslizar la app fuera de recientes, super.onDestroy()
    // (QtActivityBase -> QtNative.terminateQtNativeApplication, sincrono en
    // el hilo principal) se ha visto colgado 20+ segundos en dispositivos
    // reales -- confirmado con el volcado de dropbox del ANR, dos veces
    // (la primera se penso arreglada con ConnectionService.onTaskRemoved(),
    // pero ese aviso llega al MISMO hilo principal y nunca se procesa si ya
    // esta atascado aqui esperando el semaforo de Qt).
    //
    // isFinishing() distingue esto de un onDestroy() por cambio de
    // configuracion (rotacion, etc.), donde la Activity se recrea al
    // instante y matar el proceso seria catastrofico -- solo es true
    // cuando de verdad se esta cerrando para siempre (finish() explicito,
    // o el sistema destruyendo la tarea al deslizarla). En ese caso no
    // hace falta un apagado ordenado de Qt: LocalStore hace commit de cada
    // mensaje al vuelo, no hay nada que perder.
    @Override
    protected void onDestroy() {
        if (isFinishing()) {
            android.os.Process.killProcess(android.os.Process.myPid());
            return;
        }
        super.onDestroy();
    }
}
