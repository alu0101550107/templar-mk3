package com.templar.phone;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;

// Puente minimo hacia las notificaciones nativas de Android, llamado desde
// ClientController.cpp via QJniObject. Qt no tiene un equivalente de
// QSystemTrayIcon en Android, asi que esto es lo mas parecido: un canal fijo
// mas una notificacion "generica" por mensaje nuevo -- mismo contenido
// generico ("Nuevo mensaje", nunca el texto real) que
// trayIcon_->showMessage() en MainWindow.cpp del escritorio.
public class NotificationHelper {
    private static final String CHANNEL_ID = "templar_messages";
    private static final int NOTIFICATION_ID = 1;
    private static final int PERMISSION_REQUEST_CODE = 1001;

    public static void ensureChannel(Context context) {
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Mensajes", NotificationManager.IMPORTANCE_HIGH);
        NotificationManagerCompat.from(context).createNotificationChannel(channel);
    }

    public static void requestPermission(Activity activity) {
        if (ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(activity,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS}, PERMISSION_REQUEST_CODE);
        }
    }

    public static void show(Context context, String title, String text) {
        try {
            if (ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                return;  // Ya se pidio en requestPermission() -- sin permiso no se puede mostrar.
            }
            NotificationCompat.Builder builder = new NotificationCompat.Builder(context, CHANNEL_ID)
                    .setSmallIcon(R.drawable.ic_notification)
                    .setContentTitle(title)
                    .setContentText(text)
                    .setAutoCancel(true)
                    .setPriority(NotificationCompat.PRIORITY_HIGH);
            NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, builder.build());
        } catch (Exception e) {
            Log.e("TemplarNotif", "fallo mostrando una notificacion", e);
        }
    }
}
