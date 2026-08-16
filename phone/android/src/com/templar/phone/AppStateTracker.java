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

    public static boolean isInForeground() {
        return startedCount > 0;
    }

    public static void register(Application app) {
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
