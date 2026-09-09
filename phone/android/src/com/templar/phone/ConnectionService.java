package com.templar.phone;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.PowerManager;

import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

// Servicio en primer plano "vacio": su unico proposito es mantener el
// PROCESO entero (donde vive tanto la Activity de Qt como el codigo nativo
// de NetworkManager -- un mismo proceso, no dos) exento de los limites de
// ejecucion/red en segundo plano que Android aplica muy pronto (unos
// segundos) a una app sin ningun foreground service activo. No reimplementa
// nada de la logica de red -- el socket real lo sigue gestionando
// templar::client::NetworkManager en C++, esto solo evita que el sistema
// operativo lo mate al minimizar la app.
public class ConnectionService extends Service {
    private static final String CHANNEL_ID = "templar_service";
    private static final int NOTIFICATION_ID = 2;

    // Confirmado en un dispositivo real: con SOLO el foreground service, el
    // socket TCP se queda ESTABLISHED de verdad al minimizar (visto en
    // /proc/net/tcp), pero un mensaje que llega no se procesa -- ni
    // aparece en el chat, ni salta la notificacion -- hasta que se reabre
    // la app; entonces llegan todos de golpe. El foreground service exime
    // de que MATEN el proceso, pero no de que Doze le quite CPU real
    // mientras la pantalla esta apagada: los datos se quedan en el buffer
    // del kernel sin que el hilo de Qt llegue a que lo programen para
    // leerlos. Un PARTIAL_WAKE_LOCK mantiene la CPU despierta (no la
    // pantalla) mientras dura la conexion, para que se procesen segun
    // llegan.
    private PowerManager.WakeLock wakeLock_;

    @Override
    public void onCreate() {
        super.onCreate();

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Conexion en segundo plano", NotificationManager.IMPORTANCE_LOW);
        channel.setShowBadge(false);
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(channel);

        Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_notification)
                .setContentTitle("Templar")
                .setContentText("Conectado en segundo plano")
                .setOngoing(true)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build();

        // El tipo (remoteMessaging) viene del atributo android:foregroundServiceType
        // del <service> en el manifest -- no hace falta repetirlo aqui, la
        // sobrecarga de 2 argumentos ya lo respeta desde Android 10 en adelante.
        startForeground(NOTIFICATION_ID, notification);

        PowerManager powerManager = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock_ = powerManager.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK, "Templar::ConnectionService");
        wakeLock_.acquire();
    }

    @Override
    public void onDestroy() {
        if (wakeLock_ != null && wakeLock_.isHeld()) wakeLock_.release();
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // START_STICKY: si Android mata el proceso igualmente (memoria muy
        // baja), que lo reintente en cuanto pueda en vez de dejarlo muerto.
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // Sin esto: al deslizar la app fuera de recientes, Android llama a
    // Activity.onDestroy(), que en Qt dispara un apagado COMPLETO del
    // runtime nativo (QtActivityBase.onDestroy -> terminateQtNativeApplication,
    // sincrono, en el hilo principal). Ese apagado se ha visto colgado 20+
    // segundos en un dispositivo real (rastreado con logcat + el volcado de
    // dropbox del ANR: qtMainLoopThread y el hilo de render de Quick estaban
    // ociosos, esperando normalmente -- el propio apagado de Qt es el que
    // tarda), lo que dispara un ANR de "executing service" pasado el
    // timeout de 20s de Android, incluso con el usuario ya en otra app.
    // Deslizar la tarea es una señal inequivoca de "quiero cerrar esto
    // YA" -- no hace falta un apagado ordenado: LocalStore hace commit de
    // cada mensaje al vuelo (sin transacciones en bloque, ver
    // LocalStore::appendHistoryLine), asi que no hay nada que perder por
    // matar el proceso en el acto en vez de esperar a que Qt termine de
    // desmontar el motor QML/engine nativo.
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        super.onTaskRemoved(rootIntent);
        stopSelf();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    public static void start(Context context) {
        ContextCompat.startForegroundService(context, new Intent(context, ConnectionService.class));
    }

    public static void stop(Context context) {
        context.stopService(new Intent(context, ConnectionService.class));
    }
}
