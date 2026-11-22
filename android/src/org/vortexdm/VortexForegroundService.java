package org.vortexdm;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.app.Activity;
import android.content.ContentValues;
import android.content.ContentResolver;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import android.os.Build;
import android.os.IBinder;

public final class VortexForegroundService extends Service {
    public static void start(Context context) {
        Intent intent = new Intent(context, VortexForegroundService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(intent);
        else context.startService(intent);
    }

    // Android 9 (API 28): launch this intent from the Qt activity and pass the
    // returned URI to copyToSafUri() from onActivityResult/onActivityResult equivalent.
    public static Intent createLegacySafExportIntent(String displayName, String mimeType) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) return null;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeType == null || mimeType.isEmpty() ? "application/octet-stream" : mimeType);
        intent.putExtra(Intent.EXTRA_TITLE, displayName);
        return intent;
    }

    public static boolean copyToSafUri(Context context, String sourcePath, Uri destination) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q || destination == null) return false;
        File source = new File(sourcePath);
        if (!source.isFile() || !source.canRead()) return false;
        try (InputStream input = new FileInputStream(source);
             OutputStream output = context.getContentResolver().openOutputStream(destination, "w")) {
            if (output == null) return false;
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) != -1) output.write(buffer, 0, read);
            output.flush();
            return true;
        } catch (Exception error) {
            return false;
        }
    }

    public static boolean exportToDownloads(Context context, String sourcePath, String displayName) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false;
        File source = new File(sourcePath);
        if (!source.isFile() || !source.canRead()) return false;
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/VortexDM");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri destination = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (destination == null) return false;
        try (InputStream input = new FileInputStream(source);
             OutputStream output = resolver.openOutputStream(destination, "w")) {
            if (output == null) throw new java.io.IOException("Unable to open Downloads output");
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) != -1) output.write(buffer, 0, read);
            output.flush();
            ContentValues ready = new ContentValues();
            ready.put(MediaStore.MediaColumns.IS_PENDING, 0);
            resolver.update(destination, ready, null, null);
            return true;
        } catch (Exception error) {
            resolver.delete(destination, null, null);
            return false;
        }
    }

    @Override public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, buildNotification());
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        return START_NOT_STICKY;
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public void onDestroy() {
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationChannel channel = new NotificationChannel(
            CHANNEL_ID, "VortexDM downloads", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Keeps active VortexDM downloads running");
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) manager.createNotificationChannel(channel);
    }

    private Notification buildNotification() {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
            ? new Notification.Builder(this, CHANNEL_ID) : new Notification.Builder(this);
        return builder.setContentTitle("VortexDM")
            .setContentText("Download service is running")
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setOngoing(true).build();
    }
}
