package com.templar.phone;

import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.biometrics.BiometricManager;
import android.hardware.biometrics.BiometricPrompt;
import android.os.Build;
import android.os.CancellationSignal;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

// Igual que BiometricHelper.java en Crusader (mismo proyecto de partida,
// ver ese archivo para el razonamiento completo): BiometricPrompt de
// PLATAFORMA (android.hardware.biometrics, API 28+), no androidx.biometric
// -- esa exige un FragmentActivity como host y QtActivity (o aqui
// TemplarActivity, que extiende QtActivity) hereda de Activity a secas.
//
// Diferencia real con Crusader: ClientController::login() necesita usuario
// Y contraseña, y de proposito nunca guarda la contraseña en ningun sitio
// (ver pendingPassword_.clear() tras cada login en ClientController.cpp) --
// decision de diseño ya tomada antes de esto. Activar la huella significa
// invertir esa decision a proposito: la contraseña pasa a guardarse, pero
// SOLO cifrada con una clave del Keystore que el sistema operativo se niega
// a soltar sin verificacion biometrica. El usuario no hace falta guardarlo
// aqui -- ClientController ya lo recuerda en claro via
// rememberedUsername()/QSettings (nunca fue secreto), asi que esta clase
// solo se ocupa de la contraseña.
public class BiometricHelper {
    private static final String TAG = "TemplarBiometric";
    private static final String KEY_ALIAS = "templar_login_password_key";
    private static final String PREFS_NAME = "templar_biometric";
    private static final String PREF_CIPHERTEXT = "ciphertext";
    private static final String PREF_IV = "iv";
    private static final int GCM_TAG_LENGTH_BITS = 128;
    private static final String TRANSFORMATION = KeyProperties.KEY_ALGORITHM_AES + "/"
            + KeyProperties.BLOCK_MODE_GCM + "/" + KeyProperties.ENCRYPTION_PADDING_NONE;

    // Implementadas en BiometricBridge.cpp. Pueden llegar en un hilo
    // distinto al de Qt -- BiometricBridge se encarga de saltar de vuelta
    // al hilo principal antes de tocar nada de Qt/QML.
    private static native void nativeOnEnableResult(boolean success, String errorMessage);
    private static native void nativeOnUnlockResult(boolean success, String password, String errorMessage);

    public static boolean isAvailable(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false;
        BiometricManager manager = context.getSystemService(BiometricManager.class);
        if (manager == null) return false;
        try {
            return manager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)
                    == BiometricManager.BIOMETRIC_SUCCESS;
        } catch (Exception e) {
            Log.w(TAG, "canAuthenticate() fallo", e);
            return false;
        }
    }

    public static boolean isEnabled(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.contains(PREF_CIPHERTEXT);
    }

    public static void disable(Context context) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit().clear().apply();
        try {
            KeyStore keyStore = KeyStore.getInstance("AndroidKeyStore");
            keyStore.load(null);
            if (keyStore.containsAlias(KEY_ALIAS)) {
                keyStore.deleteEntry(KEY_ALIAS);
            }
        } catch (Exception e) {
            Log.w(TAG, "No se pudo borrar la clave del Keystore", e);
        }
    }

    private static SecretKey getOrCreateKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance("AndroidKeyStore");
        keyStore.load(null);
        if (!keyStore.containsAlias(KEY_ALIAS)) {
            KeyGenerator keyGenerator = KeyGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            KeyGenParameterSpec spec = new KeyGenParameterSpec.Builder(KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setUserAuthenticationRequired(true)
                    // Si se registra una huella nueva en el telefono, esta
                    // clave se invalida sola -- evita que baste con anadir
                    // una huella propia en un movil robado ya desbloqueado.
                    .setInvalidatedByBiometricEnrollment(true)
                    .build();
            keyGenerator.init(spec);
            keyGenerator.generateKey();
        }
        return (SecretKey) keyStore.getKey(KEY_ALIAS, null);
    }

    public static void enable(Context context, String password) {
        try {
            SecretKey key = getOrCreateKey();
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.ENCRYPT_MODE, key);
            authenticate(context, new BiometricPrompt.CryptoObject(cipher), true, password);
        } catch (Exception e) {
            Log.e(TAG, "enable() fallo antes de mostrar el dialogo", e);
            nativeOnEnableResult(false, String.valueOf(e.getMessage()));
        }
    }

    public static void unlock(Context context) {
        try {
            SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            String ivBase64 = prefs.getString(PREF_IV, null);
            if (ivBase64 == null) {
                nativeOnUnlockResult(false, null, "Huella no configurada");
                return;
            }
            byte[] iv = Base64.decode(ivBase64, Base64.NO_WRAP);

            SecretKey key = getOrCreateKey();
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(GCM_TAG_LENGTH_BITS, iv));
            authenticate(context, new BiometricPrompt.CryptoObject(cipher), false, null);
        } catch (Exception e) {
            Log.e(TAG, "unlock() fallo antes de mostrar el dialogo", e);
            nativeOnUnlockResult(false, null, String.valueOf(e.getMessage()));
        }
    }

    private static void authenticate(Context context, BiometricPrompt.CryptoObject cryptoObject,
                                      boolean isEnabling, String passwordToSave) {
        Executor executor = Executors.newSingleThreadExecutor();
        BiometricPrompt prompt = new BiometricPrompt.Builder(context)
                .setTitle("Templar")
                .setSubtitle(isEnabling
                        ? "Confirma tu huella para activar el inicio de sesión"
                        : "Iniciar sesión")
                .setNegativeButton("Cancelar", executor, (dialog, which) -> {
                    if (isEnabling) nativeOnEnableResult(false, "Cancelado");
                    else nativeOnUnlockResult(false, null, "Cancelado");
                })
                .build();

        prompt.authenticate(cryptoObject, new CancellationSignal(), executor,
                new BiometricPrompt.AuthenticationCallback() {
                    @Override
                    public void onAuthenticationSucceeded(BiometricPrompt.AuthenticationResult result) {
                        try {
                            Cipher cipher = result.getCryptoObject().getCipher();
                            SharedPreferences prefs = context.getSharedPreferences(
                                    PREFS_NAME, Context.MODE_PRIVATE);
                            if (isEnabling) {
                                byte[] ciphertext = cipher.doFinal(
                                        passwordToSave.getBytes(StandardCharsets.UTF_8));
                                prefs.edit()
                                        .putString(PREF_CIPHERTEXT,
                                                Base64.encodeToString(ciphertext, Base64.NO_WRAP))
                                        .putString(PREF_IV,
                                                Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP))
                                        .apply();
                                nativeOnEnableResult(true, null);
                            } else {
                                String ciphertextBase64 = prefs.getString(PREF_CIPHERTEXT, null);
                                byte[] ciphertext = Base64.decode(ciphertextBase64, Base64.NO_WRAP);
                                byte[] plaintext = cipher.doFinal(ciphertext);
                                nativeOnUnlockResult(true,
                                        new String(plaintext, StandardCharsets.UTF_8), null);
                            }
                        } catch (Exception e) {
                            Log.e(TAG, "Fallo al cifrar/descifrar tras autenticar", e);
                            if (isEnabling) nativeOnEnableResult(false, String.valueOf(e.getMessage()));
                            else nativeOnUnlockResult(false, null, String.valueOf(e.getMessage()));
                        }
                    }

                    @Override
                    public void onAuthenticationError(int errorCode, CharSequence errString) {
                        String message = errString != null ? errString.toString() : "Error desconocido";
                        if (isEnabling) nativeOnEnableResult(false, message);
                        else nativeOnUnlockResult(false, null, message);
                    }

                    @Override
                    public void onAuthenticationFailed() {
                        // Huella no reconocida: el dialogo del sistema sigue
                        // abierto y deja reintentar solo. No hay que
                        // resolver nada todavia.
                    }
                });
    }
}
