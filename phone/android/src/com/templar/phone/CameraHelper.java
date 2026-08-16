package com.templar.phone;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.provider.MediaStore;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.IOException;

// Lanza la camara nativa de Android (ACTION_IMAGE_CAPTURE) y manda la foto
// resultante de vuelta a C++ via un metodo native. Delega enteramente en la
// app de camara del sistema en vez de usar Camera2/CameraX directamente --
// no hace falta el permiso CAMERA ni gestionar una vista previa propia,
// solo recibimos el archivo ya hecho.
public class CameraHelper {
    private static final int REQUEST_CODE = 4242;
    private static String pendingPhotoPath;

    public static void capturePhoto(Activity activity, String authority) {
        try {
            // getFilesDir() (no getCacheDir()): qtprovider_paths.xml, que
            // genera Qt en cada build, solo cubre files/external/external-
            // cache -- no cache interno. Usar un tipo de ruta que si esta
            // cubierto evita tener que anadir un provider propio.
            File dir = new File(activity.getFilesDir(), "camera_captures");
            if (!dir.exists()) dir.mkdirs();
            File photoFile = File.createTempFile("templar_", ".jpg", dir);
            pendingPhotoPath = photoFile.getAbsolutePath();

            Uri photoUri = FileProvider.getUriForFile(activity, authority, photoFile);
            Intent intent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            intent.putExtra(MediaStore.EXTRA_OUTPUT, photoUri);
            intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);

            if (intent.resolveActivity(activity.getPackageManager()) == null) {
                nativeOnPhotoCaptured("", false);
                return;
            }
            activity.startActivityForResult(intent, REQUEST_CODE);
        } catch (IOException e) {
            nativeOnPhotoCaptured("", false);
        }
    }

    public static void onActivityResult(int requestCode, int resultCode) {
        if (requestCode != REQUEST_CODE) return;
        boolean success = resultCode == Activity.RESULT_OK;
        nativeOnPhotoCaptured(success ? pendingPhotoPath : "", success);
        pendingPhotoPath = null;
    }

    private static native void nativeOnPhotoCaptured(String path, boolean success);
}
