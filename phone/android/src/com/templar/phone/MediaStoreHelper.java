package com.templar.phone;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.webkit.MimeTypeMap;

// Escribe los archivos recibidos por chat directamente en las colecciones
// publicas de MediaStore (Galeria para fotos, la app de Archivos/Descargas
// para el resto) en vez del directorio privado de la app, que ninguna otra
// app puede ver -- ver ClientController::startBlobDownload, que decide cual
// de los dos metodos llamar segun la extension del archivo.
//
// Solo funciona desde Android 10 (API 29, "scoped storage"): antes de eso
// MediaStore.Downloads y la columna RELATIVE_PATH no existen, y escribir en
// las carpetas publicas exigiria pedir el permiso WRITE_EXTERNAL_STORAGE en
// tiempo de ejecucion, que esta app no gestiona en ningun otro punto -- por
// debajo de esa version openImageOutputFd/openDownloadOutputFd devuelven -1
// sin tocar nada, y ClientController cae de vuelta al fichero privado de
// siempre.
//
// El flujo "IS_PENDING" es el que exige MediaStore para escrituras por
// streaming: se inserta la fila marcada como pendiente (invisible para
// Galeria/Archivos mientras tanto), se abre un descriptor hacia ella, y
// solo cuando ClientController termina de escribir (ver finishPending,
// llamado desde onBlobEndReceived/onBlobNotFound/cancelActiveTransfers) se
// aclara la marca o se borra la fila entera si la descarga fallo del todo.
// Basta con recordar la ultima fila insertada: ClientController ya
// garantiza que solo hay una descarga activa a la vez (ver el "Ya hay una
// descarga en curso" de startBlobDownload).
public class MediaStoreHelper {
    private static Uri pendingUri;

    public static int openImageOutputFd(Context context, String displayName) {
        return openOutputFd(context, MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                "Pictures/Templar", displayName);
    }

    public static int openDownloadOutputFd(Context context, String displayName) {
        return openOutputFd(context, MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                "Download", displayName);
    }

    private static int openOutputFd(Context context, Uri collection, String relativePath,
                                     String displayName) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return -1;
        try {
            String ext = MimeTypeMap.getFileExtensionFromUrl(displayName);
            String mime = ext != null ? MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) : null;
            if (mime == null) mime = fallbackMimeType(ext);

            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
            values.put(MediaStore.MediaColumns.MIME_TYPE, mime);
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, relativePath);
            values.put(MediaStore.MediaColumns.IS_PENDING, 1);

            ContentResolver resolver = context.getContentResolver();
            Uri itemUri = resolver.insert(collection, values);
            if (itemUri == null) return -1;

            ParcelFileDescriptor pfd = resolver.openFileDescriptor(itemUri, "w");
            if (pfd == null) {
                resolver.delete(itemUri, null, null);
                return -1;
            }

            pendingUri = itemUri;
            return pfd.detachFd();
        } catch (Exception e) {
            return -1;
        }
    }

    // MimeTypeMap sale de la tabla de tipos de WebKit (pensada para
    // contenido web) y no conoce varias extensiones de uso comun en
    // Android que no son de origen web -- "apk" es el caso mas notorio:
    // sin esto se guardaba como application/octet-stream generico, y con
    // un MIME tan ambiguo el instalador de paquetes / gestores de archivos
    // del sistema dejan de reconocer el archivo como instalable aunque
    // los bytes lleguen intactos.
    private static String fallbackMimeType(String ext) {
        if (ext != null && ext.equalsIgnoreCase("apk")) {
            return "application/vnd.android.package-archive";
        }
        return "application/octet-stream";
    }

    // keep=true: aclara IS_PENDING para que la fila se vuelva visible en
    // Galeria/Descargas -- se llama tanto si la descarga termino bien como
    // si se corto a medias (mismo criterio que el fichero privado de
    // siempre: lo ya escrito se queda, nunca se borra por una descarga
    // incompleta salvo que el blob no exista en absoluto en el servidor).
    // keep=false: la fila se borra entera (blob no encontrado en el
    // servidor, o el descriptor no llego a abrirse bien).
    public static void finishPending(Context context, boolean keep) {
        if (pendingUri == null) return;
        Uri uri = pendingUri;
        pendingUri = null;
        try {
            ContentResolver resolver = context.getContentResolver();
            if (keep) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.IS_PENDING, 0);
                resolver.update(uri, values, null, null);
            } else {
                resolver.delete(uri, null, null);
            }
        } catch (Exception e) {
            // No hay nada mas que hacer si esto falla -- en el peor caso
            // queda una fila IS_PENDING=1 huerfana (invisible para el resto
            // de apps), no un archivo roto visible en Galeria/Descargas.
        }
    }
}
