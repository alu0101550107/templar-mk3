package com.templar.phone;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.webkit.MimeTypeMap;

import androidx.core.content.FileProvider;

import java.io.File;

// Abre un archivo local (adjuntado en el chat CONTIGO MISMO, ver
// ClientController::openSelfFile) con la app del sistema que corresponda a
// su tipo (visor de PDF, galeria, etc.) -- mismo mecanismo de FileProvider
// que CameraHelper.java usa para entregarle una foto a la app de camara,
// pero al reves: aqui es TEMPLAR quien le pasa un archivo propio a OTRA
// app, asi que hace falta FLAG_GRANT_READ_URI_PERMISSION en vez de
// _WRITE_.
public class FileOpenHelper {
    public static void openFile(Activity activity, String authority, String path) {
        try {
            File file = new File(path);
            Uri uri = FileProvider.getUriForFile(activity, authority, file);

            String ext = MimeTypeMap.getFileExtensionFromUrl(path);
            String mime = ext != null ? MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) : null;
            if (mime == null) mime = "*/*";

            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(uri, mime);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            // Ninguna app instalada sabe abrir ese tipo -- no hay nada mas
            // que hacer aqui (mismo criterio silencioso que el resto de la
            // app en fallos no criticos de UI; el usuario simplemente no
            // vera pasar nada).
        }
    }
}
