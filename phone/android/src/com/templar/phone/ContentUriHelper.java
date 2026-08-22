package com.templar.phone;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

// El selector de archivos del sistema (Storage Access Framework) entrega un
// content://, no una ruta de fichero real -- QUrl::fileName() de Qt en esas
// URIs solo lee el ultimo segmento, que es un ID opaco del proveedor
// (p.ej. "msf:1000000123"), nunca el nombre real del archivo ni su
// extension. Ver ClientController::resolveOriginalFilename, que llama a
// esto para sendFile/startSelfFileAttach -- una foto capturada con la
// camara (ver CameraHelper.java) no necesita esto porque esa llega como
// file:// normal, con el nombre real ya en la propia ruta.
public class ContentUriHelper {
    public static String queryDisplayName(Context context, String uriString) {
        try {
            Uri uri = Uri.parse(uriString);
            Cursor cursor = context.getContentResolver().query(
                    uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null);
            if (cursor == null) return "";
            try {
                if (cursor.moveToFirst()) {
                    int idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (idx >= 0) {
                        String name = cursor.getString(idx);
                        if (name != null) return name;
                    }
                }
            } finally {
                cursor.close();
            }
        } catch (Exception e) {
            // Se cae al fallback de QUrl::fileName() en C++.
        }
        return "";
    }
}
