package com.rubidiumclient.host;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Environment;
import android.os.SystemClock;
import android.content.ContentValues;
import android.net.Uri;
import android.provider.MediaStore;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStream;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.widget.Toast;
import com.mojang.minecraftpe.platforms.Platform2;

/** Mount one overlay per resumed activity; menu touches never cover the entire game. */
public final class ApollonOverlay implements Application.ActivityLifecycleCallbacks {
    private static final String TAG = "EClientApollon";
    private static boolean registered;
    private static boolean logInitialized;
    private static File privateLog;
    private static Uri downloadsLog;
    private static long lastLogCopy;
    private final Handler handler = new Handler();
    private Activity activity;
    private WindowManager wm;
    private Platform2 surface;
    private View touch;
    private WindowManager.LayoutParams touchParams;
    private boolean keyboardVisible;
    private Activity resumedActivity;
    private int mountAttempts;
    private final Runnable retryMount = () -> {
        Activity current = resumedActivity;
        if (current != null && !current.isFinishing() && !current.isDestroyed()) mount(current);
    };
    private final Runnable reposition = new Runnable() {
        @Override public void run() {
            if (touch == null || surface == null) return;
            try {
                if (SystemClock.elapsedRealtime() - lastLogCopy >= 2000) {
                    lastLogCopy = SystemClock.elapsedRealtime();
                    syncFly2Log();
                }
                boolean ime = isKeyboardVisible();
                if (ime != keyboardVisible) {
                    if (ime) {
                        // Never hold a Fly2/menu press across an IME transition.
                        Platform2 renderer = surface;
                        renderer.queueEvent(() -> renderer.sendTouch(false, -1, -1));
                        touchParams.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
                        touch.setVisibility(View.GONE);
                    } else {
                        touchParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
                    }
                    wm.updateViewLayout(touch, touchParams);
                    keyboardVisible = ime;
                    Log.i(TAG, ime ? "Keyboard visible; menu touch suspended" : "Keyboard closed; menu touch restored");
                }
                if (ime) {
                    handler.postDelayed(this, 20);
                    return;
                }
                String[] rect = Platform2.getWindowRect().split("\\|");
                int x = Integer.parseInt(rect[0]), y = Integer.parseInt(rect[1]);
                int w = Integer.parseInt(rect[2]), h = Integer.parseInt(rect[3]);
                int width = surface.getWidth(), height = surface.getHeight();
                if (w <= 0 || h <= 0 || width <= 0 || height <= 0) {
                    touch.setVisibility(View.GONE);
                } else {
                    int left = Math.max(0, Math.min(x, width - 1));
                    int top = Math.max(0, Math.min(y, height - 1));
                    int right = (int)Math.max(left + 1L, Math.min((long)width, (long)x + w));
                    int bottom = (int)Math.max(top + 1L, Math.min((long)height, (long)y + h));
                    int[] origin = new int[2];
                    surface.getLocationOnScreen(origin);
                    touchParams.x = origin[0] + left; touchParams.y = origin[1] + top;
                    touchParams.width = right - left; touchParams.height = bottom - top;
                    wm.updateViewLayout(touch, touchParams);
                    touch.setVisibility(View.VISIBLE);
                }
            } catch (Exception e) { Log.w(TAG, "Overlay position unavailable", e); }
            handler.postDelayed(this, 20);
        }
    };
    private boolean isKeyboardVisible() {
        if (activity == null) return false;
        View decor = activity.getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsets insets = decor.getRootWindowInsets();
            if (insets != null) return insets.isVisible(WindowInsets.Type.ime());
        }
        // Older Android: use the visible display frame as a fallback. Avoid
        // treating a smaller split-screen window as a permanently open IME.
        if (activity.isInMultiWindowMode()) return false;
        Rect visible = new Rect();
        decor.getWindowVisibleDisplayFrame(visible);
        Point display = new Point();
        activity.getWindowManager().getDefaultDisplay().getRealSize(display);
        int threshold = (int)(120 * activity.getResources().getDisplayMetrics().density);
        return visible.bottom > visible.top && display.y - visible.bottom > threshold;
    }
    public static void install(Context context) {
        if (registered) return;
        Context app = context.getApplicationContext();
        if (!(app instanceof Application)) { Log.e(TAG, "Application unavailable"); return; }
        registered = true;
        ((Application)app).registerActivityLifecycleCallbacks(new ApollonOverlay());
    }
    private WindowManager.LayoutParams params(int w, int h, int flags) {
        WindowManager.LayoutParams p = new WindowManager.LayoutParams(w, h,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL, flags,
                PixelFormat.TRANSLUCENT);
        p.token = activity.getWindow().getDecorView().getWindowToken();
        p.gravity = Gravity.TOP | Gravity.LEFT;
        // With NOT_FOCUSABLE alone Android normally layers the panel ABOVE
        // the IME. ALT_FOCUSABLE_IM keeps it behind the keyboard without
        // making this panel focusable or taking the game's text focus.
        p.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
        return p;
    }
    // Native code writes a private staging file; Android scoped storage publishes
    // one owned Downloads entry, replacing its contents, not adding per-run files.
    private static void initializeFly2Log(Context context) {
        if (logInitialized) return;
        logInitialized = true;
        privateLog = new File(context.getFilesDir(), "fly2test.txt");
        try (FileOutputStream out = new FileOutputStream(privateLog, false)) {
            out.write("E-Client Fly2 test log\n".getBytes("UTF-8"));
        } catch (Exception e) { Log.e(TAG, "Unable to initialize Fly2 log", e); privateLog = null; return; }
        Platform2.setFly2LogPath(privateLog.getAbsolutePath());
        if (Build.VERSION.SDK_INT >= 29) {
            String saved = context.getSharedPreferences("eclient_fly2", 0).getString("downloads_uri", null);
            if (saved != null) {
                try (OutputStream test = context.getContentResolver().openOutputStream(Uri.parse(saved), "wt")) {
                    if (test != null) downloadsLog = Uri.parse(saved);
                } catch (Exception e) { Log.w(TAG, "Previous Downloads log unavailable", e); }
            }
            if (downloadsLog == null) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.DISPLAY_NAME, "fly2test.txt");
                values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                try {
                    downloadsLog = context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                    if (downloadsLog != null) context.getSharedPreferences("eclient_fly2", 0).edit().putString("downloads_uri", downloadsLog.toString()).apply();
                } catch (Exception e) { Log.e(TAG, "Unable to create Downloads log", e); }
            }
        }
        syncFly2Log(context);
    }
    private void syncFly2Log() { if (activity != null) syncFly2Log(activity); }
    private static void syncFly2Log(Context context) {
        if (privateLog == null) return;
        try (FileInputStream input = new FileInputStream(privateLog)) {
            OutputStream destination;
            if (Build.VERSION.SDK_INT >= 29) {
                if (downloadsLog == null) return;
                destination = context.getContentResolver().openOutputStream(downloadsLog, "wt");
            } else {
                destination = new FileOutputStream(new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "fly2test.txt"), false);
            }
            if (destination == null) return;
            try (OutputStream output = destination) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) output.write(buffer, 0, n);
            }
        } catch (Exception e) { Log.w(TAG, "Unable to update Downloads/fly2test.txt", e); }
    }
    private void mount(Activity a) {
        if (resumedActivity != a || activity == a) return;
        if (a.getWindow().getDecorView().getWindowToken() == null) {
            scheduleMountRetry();
            return;
        }
        unmount();
        activity = a;
        keyboardVisible = false;
        wm = a.getWindowManager();
        try {
            initializeFly2Log(a);
            surface = new Platform2(a);
            wm.addView(surface, params(-1, -1, WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE));
            touch = new View(a);
            touchParams = params(1, 1, WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            touch.setVisibility(View.GONE);
            touch.setOnTouchListener((v, event) -> {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE ||
                    action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    final boolean down = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL;
                    Platform2 renderer = surface;
                    if (renderer == null) return false;
                    int[] origin = new int[2];
                    renderer.getLocationOnScreen(origin);
                    final float x = event.getRawX() - origin[0], y = event.getRawY() - origin[1];
                    if (renderer != null) renderer.queueEvent(() -> renderer.sendTouch(down, x, y));
                    return true;
                }
                return false;
            });
            wm.addView(touch, touchParams);
            handler.post(reposition);
            mountAttempts = 0;
            handler.removeCallbacks(retryMount);
            Log.i(TAG, "Overlay mounted");
        } catch (RuntimeException e) {
            Log.w(TAG, "Overlay mount not ready; retrying", e);
            unmount();
            scheduleMountRetry();
        }
    }
    private void scheduleMountRetry() {
        handler.removeCallbacks(retryMount);
        if (resumedActivity == null) return;
        if (++mountAttempts <= 40) handler.postDelayed(retryMount, 250);
        else {
            Log.e(TAG, "Overlay could not mount after retries");
            Toast.makeText(resumedActivity, "E-Client overlay could not start; see logcat", Toast.LENGTH_LONG).show();
        }
    }
    private void unmount() {
        syncFly2Log();
        handler.removeCallbacks(reposition);
        if (wm != null) {
            if (touch != null && touch.isAttachedToWindow()) wm.removeViewImmediate(touch);
            if (surface != null && surface.isAttachedToWindow()) {
                Platform2 oldSurface = surface;
                oldSurface.queueEvent(oldSurface::releaseRenderer);
                wm.removeViewImmediate(surface);
            }
        }
        touch = null; surface = null; wm = null; activity = null;
    }
    @Override public void onActivityResumed(Activity a) {
        handler.removeCallbacks(retryMount);
        resumedActivity = a;
        mountAttempts = 0;
        mount(a);
    }
    @Override public void onActivityPaused(Activity a) {
        if (resumedActivity == a) { resumedActivity = null; handler.removeCallbacks(retryMount); }
        if (activity == a) unmount();
    }
    @Override public void onActivityCreated(Activity a, Bundle b) {}
    @Override public void onActivityStarted(Activity a) {}
    @Override public void onActivityStopped(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    @Override public void onActivityDestroyed(Activity a) {
        if (resumedActivity == a) { resumedActivity = null; handler.removeCallbacks(retryMount); }
        if (activity == a) unmount();
    }
}
