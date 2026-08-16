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
}
