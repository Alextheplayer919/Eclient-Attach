package com.rubidiumclient.modules

/**
 * JNI context for the Kotlin module layer — implemented in
 * native/runtime/src/kotlin/KotlinBridge.cpp (libeclient_host.so).
 *
 * Every symbol below must exist in that file under the exact mangled name
 * Java_com_rubidiumclient_modules_NativeBridge_<name>. A typo here fails at
 * call time (UnsatisfiedLinkError), not at build time — so treat this file as
 * an ABI list, not as a convenience wrapper.
 *
 * Under baksmali these land as instance methods on the NativeBridge singleton,
 * which is why the native exports take (JNIEnv*, jobject).
 *
 * Player-state calls only return data while a tick dispatch is in flight (the
 * bridge stashes the local player for the current tick); elsewhere they return
 * false / defaults by design.
 */
object NativeBridge {

    // ── player state ──────────────────────────────────────────────────────

    external fun getPosition(out: FloatArray): Boolean
    external fun getVelocity(out: FloatArray): Boolean
    external fun setVelocity(x: Float, y: Float, z: Float)

    /**
     * World-space horizontal input direction (yaw-corrected, normalized;
     * out[2] = raw magnitude). Same accel-residue measurement the old C++ fly
     * used: the joystick IS vanilla keyboard-equivalent input, so this reads
     * the same move vector the game itself produces. false = below deadzone.
     */
    external fun getWorldInputDir(out: FloatArray): Boolean

    // ── native module surface (the C++ ModuleManager) ─────────────────────
    //
    // One call returns the whole list so the UI never does N JNI round-trips:
    // each line is  name \t category \t enabled(0|1) \t memoryBacked(0|1) \t description

    external fun nativeListModules(): String

    external fun nativeModuleEnabled(name: String): Boolean

    /** Apply a toggle. Returns false when the patch layer refused (profile not validated). */
    external fun nativeSetModuleEnabled(name: String, enabled: Boolean): Boolean

    /** Human-readable patch-profile state for the panel's status line. */
    external fun nativePatchStatus(): String

    /**
     * Tells the native tick gate whether it should dispatch Kotlin ticks at all.
     * Called by ModuleRegistry whenever the Kotlin side's enable state changes.
     */
    external fun setKotlinTickActive(active: Boolean)

    // ── native settings (read-only) — for modules that must respect a native
    //    module's configuration, e.g. a bot reading KillAura2's Range ──────

    external fun getFloatSetting(module: String, key: String, fallback: Float): Float
    external fun getBoolSetting(module: String, key: String, fallback: Boolean): Boolean

    // ── logcat, tag EClientKotlin (also tee'd to Download/EClient/*.txt) ──

    external fun logLine(line: String)
}
