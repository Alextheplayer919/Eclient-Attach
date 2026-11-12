package com.mojang.minecraftpe.platforms;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** A separate transparent GL surface: Apollon must not clear Minecraft's surface. */
public final class Platform2 extends GLSurfaceView implements GLSurfaceView.Renderer {
    // Old GL threads may drain queued work after a new overlay has mounted.
    // Serialize JNI ImGui access and give every surface a distinct generation.
    private static final Object rendererLock = new Object();
    private static long currentGeneration;
    private static boolean nativeInitialized;
    private final long generation;
    public Platform2(Context context) {
        super(context);
        synchronized (rendererLock) { generation = ++currentGeneration; }
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setRenderer(this);
    }
    @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        synchronized (rendererLock) {
            if (generation != currentGeneration) return;
            // A context can be lost without a queued shutdown running. Rebuild
            // ImGui and its renderer in this new GL context, with fresh style.
            if (nativeInitialized) { imgui_Shutdown(); nativeInitialized = false; }
            init(); nativeInitialized = true;
        }
    }
    @Override public void onSurfaceChanged(GL10 gl, int width, int height) {
        synchronized (rendererLock) { if (generation == currentGeneration && nativeInitialized) resize(width, height); }
    }
    @Override public void onDrawFrame(GL10 gl) {
        synchronized (rendererLock) { if (generation == currentGeneration && nativeInitialized) step(); }
    }
    public void sendTouch(boolean down, float x, float y) {
        synchronized (rendererLock) { if (generation == currentGeneration && nativeInitialized) MotionEventClick(down, x, y); }
    }
    public void releaseRenderer() {
        synchronized (rendererLock) {
            if (generation == currentGeneration && nativeInitialized) { imgui_Shutdown(); nativeInitialized = false; }
        }
    }
    public static native void setFly2LogPath(String path);
    public static native void setCombatLogPath(String path);
    public static native void init();
    public static native void resize(int width, int height);
    public static native void step();
    public static native void imgui_Shutdown();
    public static native void MotionEventClick(boolean down, float x, float y);
    public static native String getWindowRect();
}
