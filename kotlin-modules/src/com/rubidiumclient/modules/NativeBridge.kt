package com.rubidiumclient.modules

/**
 * JNI context for Kotlin modules. Implemented in
 * native/runtime/src/kotlin/KotlinBridge.cpp (libeclient_host.so).
 *
 * All functions only respond while a tick dispatch is in flight (the bridge
 * stashes the local player for the current tick); calling them elsewhere
 * returns false / defaults.
 *
 * Warning: under baksmali these land as virtual methods on the NativeBridge
 * singleton — matching the (JNIEnv*, jobject) JNI exports on the native side.
 */
object NativeBridge {

    // ── Player state ────────────────────────────────────────────────────
    external fun getPosition(out: FloatArray): Boolean
    external fun getVelocity(out: FloatArray): Boolean
    external fun setVelocity(x: Float, y: Float, z: Float)

    /**
     * World-space horizontal input direction (already yaw-corrected,
     * normalized; out[2] = raw magnitude). Same accel-residue measurement
     * the C++ Fly uses — the joystick IS keyboard-equivalent vanilla input.
     * Returns false when below the deadzone (no movement input this tick).
     */
    external fun getWorldInputDir(out: FloatArray): Boolean

    // ── ▲/▼ fly buttons (shared with the C++ Fly) ───────────────────────
    external fun buttonUp(): Boolean
    external fun buttonDown(): Boolean

    // ── Native ModuleManager settings (rendered in the shared menu) ─────
    external fun getFloatSetting(module: String, key: String, fallback: Float): Float
    external fun getBoolSetting(module: String, key: String, fallback: Boolean): Boolean

    // ── logcat (tag EClientKotlin) ──────────────────────────────────────
    external fun logLine(line: String)
}
