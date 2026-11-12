package com.mojang.minecraftpe.platforms;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** A separate transparent GL surface: Apollon must not clear Minecraft's surface. */
public final class Platform2 extends GLSurfaceView implements GLSurfaceView.Renderer {
    public Platform2(Context context) {
        super(context);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setRenderer(this);
    }
    @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) { init(); }
    @Override public void onSurfaceChanged(GL10 gl, int width, int height) { resize(width, height); }
    @Override public void onDrawFrame(GL10 gl) { step(); }
    public static native void setFly2LogPath(String path);
    public static native void init();
    public static native void resize(int width, int height);
    public static native void step();
    public static native void imgui_Shutdown();
    public static native void MotionEventClick(boolean down, float x, float y);
    public static native String getWindowRect();
}
