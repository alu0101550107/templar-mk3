package com.templar.phone;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;

// Sustituye a QGuiApplication::applicationState() de Qt para saber si la
// app esta en primer plano: en Android ese estado de Qt no cambia de forma
// fiable cuando la Activity se pausa pero el proceso sigue vivo gracias a
// ConnectionService, asi que hace falta preguntarselo directo a Android en
// vez de a Qt. Patron estandar (el mismo que usa internamente
// androidx.lifecycle.ProcessLifecycleOwner): contar actividades
// iniciadas-pero-no-paradas, si el contador es > 0 hay alguna visible.
public class AppStateTracker implements Application.ActivityLifecycleCallbacks {
    private static int startedCount = 0;
    private static boolean registered = false;

    public static boolean isInForeground() {
        return startedCount > 0;
    }

    // Idempotente a proposito: TemplarActivity.onCreate() puede correr mas
    // de una vez por proceso (p.ej. la Activity se recrea en un cambio de
    // configuracion) -- sin este guardado, cada onCreate() registraria OTRA
    // instancia mas como listener, y startedCount se incrementaria varias
    // veces por cada evento real de arranque/parada.
    public static void register(Application app) {
        if (registered) return;
        registered = true;
        app.registerActivityLifecycleCallbacks(new AppStateTracker());
    }

    @Override
    public void onActivityStarted(Activity activity) {
        startedCount++;
    }

    @Override
    public void onActivityStopped(Activity activity) {
        startedCount--;
    }

    @Override
    public void onActivityCreated(Activity activity, Bundle savedInstanceState) {}

    @Override
    public void onActivityResumed(Activity activity) {}

    @Override
    public void onActivityPaused(Activity activity) {}

    @Override
    public void onActivitySaveInstanceState(Activity activity, Bundle outState) {}

    @Override
    public void onActivityDestroyed(Activity activity) {}
}
