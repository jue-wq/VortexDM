package org.vortexdm;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.widget.Toast;
import org.qtproject.qt.android.bindings.QtActivity;

public final class VortexActivity extends QtActivity {
    private static final int SAF_EXPORT_REQUEST = 7302;
    private static VortexActivity currentActivity;
    private static String pendingSafSourcePath;

    public static boolean requestLegacySafExport(String sourcePath, String displayName, String mimeType) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q || currentActivity == null) return false;
        java.io.File source = new java.io.File(sourcePath);
        if (!source.isFile() || !source.canRead()) return false;
        Intent intent = VortexForegroundService.createLegacySafExportIntent(displayName, mimeType);
        if (intent == null) return false;
        pendingSafSourcePath = source.getAbsolutePath();
        try {
            currentActivity.startActivityForResult(intent, SAF_EXPORT_REQUEST);
            return true;
        } catch (Exception error) {
            pendingSafSourcePath = null;
            return false;
        }
    }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        currentActivity = this;
    }

    @Override protected void onDestroy() {
        if (currentActivity == this) currentActivity = null;
        super.onDestroy();
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != SAF_EXPORT_REQUEST) return;
        final String source = pendingSafSourcePath;
        pendingSafSourcePath = null;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null || source == null) {
            Toast.makeText(this, "Export cancelled", Toast.LENGTH_SHORT).show();
            return;
        }
        final Uri destination = data.getData();
        final boolean copied = VortexForegroundService.copyToSafUri(this, source, destination);
        Toast.makeText(this, copied ? "File exported" : "Export failed", Toast.LENGTH_LONG).show();
    }
}
