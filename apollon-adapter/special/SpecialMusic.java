package com.rubidiumclient.host;

import android.app.Activity;
import android.content.Context;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/** Special-edition-only music. The normal build never packages this class. */
final class SpecialMusic {
    private static final String TAG = "EClientMusic";
    private static final String ASSET = "eclient-special/e.mp3";
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static Activity resumed;
    private static MediaPlayer player;
    private static boolean loading;
    private static boolean prepared;
    private static boolean failed;

    static void resume(Activity activity) {
        resumed = activity;
        if (prepared && player != null) {
            try { if (!player.isPlaying()) player.start(); }
            catch (IllegalStateException e) { fail(e); }
            return;
        }
        if (loading || failed) return;
        loading = true;
        Context app = activity.getApplicationContext();
        // Assets may be compressed, so copy via a stream rather than openFd.
        new Thread(() -> {
            File file = new File(app.getCacheDir(), "eclient-special-music.mp3");
            try (InputStream in = app.getAssets().open(ASSET);
                 FileOutputStream out = new FileOutputStream(file, false)) {
                byte[] buffer = new byte[16384]; int n;
                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
            } catch (Exception e) {
                main.post(() -> fail(e));
                return;
            }
            main.post(() -> prepare(file));
        }, "EClientMusicLoader").start();
    }

    private static void prepare(File file) {
        try {
            MediaPlayer p = new MediaPlayer();
            player = p;
            p.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
            p.setDataSource(file.getAbsolutePath());
            p.setLooping(true);
            p.setOnPreparedListener(ready -> {
                loading = false;
                prepared = true;
                if (resumed != null) {
                    try { ready.start(); } catch (IllegalStateException e) { fail(e); }
                }
            });
            p.setOnErrorListener((ignored, what, extra) -> {
                fail(new IllegalStateException("MediaPlayer error " + what + "/" + extra));
                return true;
            });
            p.prepareAsync();
        } catch (Exception e) { fail(e); }
    }

    static void pause(Activity activity) {
        if (resumed != activity) return;
        resumed = null;
        if (prepared && player != null) {
            try { if (player.isPlaying()) player.pause(); }
            catch (IllegalStateException e) { fail(e); }
        }
    }

    private static void fail(Exception error) {
        Log.e(TAG, "Unable to play special-edition music", error);
        loading = false;
        prepared = false;
        failed = true;
        if (player != null) { player.release(); player = null; }
    }
}
