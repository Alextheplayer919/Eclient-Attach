package com.rubidiumclient.host;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.Toast;
import com.mojang.minecraftpe.platforms.Platform2;

/** Mount one overlay per resumed activity; menu touches never cover the entire game. */
public final class ApollonOverlay implements Application.ActivityLifecycleCallbacks {
    private static final String TAG = "EClientApollon";
    private static boolean registered;
    private final Handler handler = new Handler();
    private Activity activity;
    private WindowManager wm;
    private Platform2 surface;
    private View touch;
    private Button button;
    private WindowManager.LayoutParams touchParams;
    private boolean visible = true;
    private final Runnable reposition = new Runnable() {
        @Override public void run() {
            if (touch == null || surface == null) return;
            try {
                String[] rect = Platform2.getWindowRect().split("\\|");
                int x = Integer.parseInt(rect[0]), y = Integer.parseInt(rect[1]);
                int w = Integer.parseInt(rect[2]), h = Integer.parseInt(rect[3]);
                int width = surface.getWidth(), height = surface.getHeight();
                if (!visible || w <= 0 || h <= 0 || width <= 0 || height <= 0) {
                    touch.setVisibility(View.GONE);
                } else {
                    int left = Math.max(0, Math.min(x, width - 1));
                    int top = Math.max(0, Math.min(y, height - 1));
                    int right = (int)Math.max(left + 1L, Math.min((long)width, (long)x + w));
                    int bottom = (int)Math.max(top + 1L, Math.min((long)height, (long)y + h));
                    touchParams.x = left; touchParams.y = top;
                    touchParams.width = right - left; touchParams.height = bottom - top;
                    wm.updateViewLayout(touch, touchParams);
                    touch.setVisibility(View.VISIBLE);
                }
            } catch (Exception e) { Log.w(TAG, "Overlay position unavailable", e); }
            handler.postDelayed(this, 100);
        }
    };
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
        p.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
        return p;
    }
    private void mount(Activity a) {
        if (activity == a) return;
        unmount();
        activity = a;
        wm = a.getWindowManager();
        try {
            surface = new Platform2(a);
            wm.addView(surface, params(-1, -1, WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE));
            touch = new View(a);
            touchParams = params(1, 1, WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            touch.setOnTouchListener((v, event) -> {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE ||
                    action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    final boolean down = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL;
                    final float x = event.getRawX(), y = event.getRawY();
                    Platform2 renderer = surface;
                    if (renderer != null) renderer.queueEvent(() -> Platform2.MotionEventClick(down, x, y));
                    return true;
                }
                return false;
            });
            wm.addView(touch, touchParams);
            button = new Button(a);
            button.setText("E-Client");
            button.setOnClickListener(v -> {
                visible = !visible;
                surface.setVisibility(visible ? View.VISIBLE : View.INVISIBLE);
                touch.setVisibility(visible ? View.VISIBLE : View.GONE);
            });
            WindowManager.LayoutParams bp = params(-2, -2, WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            bp.gravity = Gravity.TOP | Gravity.RIGHT;
            wm.addView(button, bp);
            handler.post(reposition);
            Log.i(TAG, "Overlay mounted");
        } catch (RuntimeException e) {
            Log.e(TAG, "Overlay mount failed", e);
            Toast.makeText(a, "E-Client overlay failed; see logcat", Toast.LENGTH_LONG).show();
            unmount();
        }
    }
    private void unmount() {
        handler.removeCallbacks(reposition);
        if (wm != null) {
            if (button != null && button.isAttachedToWindow()) wm.removeViewImmediate(button);
            if (touch != null && touch.isAttachedToWindow()) wm.removeViewImmediate(touch);
            if (surface != null && surface.isAttachedToWindow()) {
                surface.queueEvent(Platform2::imgui_Shutdown);
                wm.removeViewImmediate(surface);
            }
        }
        button = null; touch = null; surface = null; wm = null; activity = null;
    }
    @Override public void onActivityResumed(Activity a) { mount(a); }
    @Override public void onActivityPaused(Activity a) { if (activity == a) unmount(); }
    @Override public void onActivityCreated(Activity a, Bundle b) {}
    @Override public void onActivityStarted(Activity a) {}
    @Override public void onActivityStopped(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    @Override public void onActivityDestroyed(Activity a) { if (activity == a) unmount(); }
}
