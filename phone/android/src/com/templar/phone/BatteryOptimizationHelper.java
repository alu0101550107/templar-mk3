package com.templar.phone;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;

// ConnectionService (el foreground service, ver ese archivo) por si solo no
// basta para que el proceso sobreviva de verdad en segundo plano en muchos
// moviles reales: es el mecanismo que exime a la app de los limites de
// Doze/App Standby, pero varios fabricantes (Xiaomi, Huawei, Oppo, Vivo,
// OnePlus...) meten ADEMAS su propio gestor de bateria por encima del de
// Android, que puede matar el proceso igual aunque el permiso de aqui abajo
// este concedido. Sin Google Play/FCM de por medio (decision deliberada de
// este proyecto) no hay forma de evitar del todo esa segunda capa -- lo
// maximo que se puede hacer es pedirle los dos permisos al usuario y
// explicarle por que hacen falta (ver SettingsDialog.qml).
public class BatteryOptimizationHelper {
    private static final String TAG = "TemplarBattery";

    public static boolean isIgnoring(Context context) {
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        return pm != null && pm.isIgnoringBatteryOptimizations(context.getPackageName());
    }

    // Dialogo OFICIAL de Android -- funciona igual en cualquier
    // fabricante, es el mecanismo estandar que Google exige respetar para
    // el sello "Android Compatible".
    public static void requestIgnore(Activity activity) {
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "el dispositivo no tiene el dialogo estandar de ignorar bateria", e);
        }
    }

    // No hay una API publica de Android para la pantalla de "autoarranque/
    // sin restricciones" propia de cada fabricante -- cada uno tiene su
    // propio nombre de paquete/actividad, sin documentar oficialmente y
    // que puede cambiar entre versiones de su ROM. Se intenta la conocida
    // para el fabricante detectado (Build.MANUFACTURER) y, si falla (no
    // existe en esta version concreta de su sistema), se cae a la
    // pantalla generica de detalles de la app, donde el usuario puede
    // buscar el ajuste el mismo.
    public static void openManufacturerSettings(Activity activity) {
        String manufacturer = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER.toLowerCase();
        Intent intent = new Intent();
        switch (manufacturer) {
            case "xiaomi":
                intent.setClassName("com.miui.securitycenter",
                        "com.miui.permcenter.autostart.AutoStartManagementActivity");
                break;
            case "huawei":
                intent.setClassName("com.huawei.systemmanager",
                        "com.huawei.systemmanager.startupmgr.ui.StartupNormalAppListActivity");
                break;
            case "oppo":
                intent.setClassName("com.coloros.safecenter",
                        "com.coloros.safecenter.permission.startup.StartupAppListActivity");
                break;
            case "vivo":
                intent.setClassName("com.vivo.permissionmanager",
                        "com.vivo.permissionmanager.activity.BgStartUpManagerActivity");
                break;
            case "oneplus":
                intent.setClassName("com.oneplus.security",
                        "com.oneplus.security.chainlaunch.view.ChainLaunchAppListActivity");
                break;
            default:
                intent = null;
                break;
        }

        if (intent != null) {
            try {
                activity.startActivity(intent);
                return;
            } catch (Exception e) {
                Log.w(TAG, "pantalla especifica de " + manufacturer + " no disponible, cayendo a la generica", e);
            }
        }

        try {
            Intent fallback = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
            fallback.setData(Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(fallback);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "ni siquiera la pantalla generica de detalles de la app esta disponible", e);
        }
    }
}
