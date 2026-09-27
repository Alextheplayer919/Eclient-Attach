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

    /**
     * Apply a toggle.
     *
     * Returns "" when it applied, or a human-readable reason when it refused —
     * e.g. a missing patch profile, a profile that failed validation, or a write
     * that did not land. Show the reason; never invent one.
     */
    external fun nativeSetModuleEnabled(name: String, enabled: Boolean): String

    /**
     * Live module-layer state for the panel: tick counter, resolved game API
     * pointers, world-ready latch, enabled modules, and the last KillAura2 scan
     * funnel. Read-only; never throws.
     */
    external fun nativeDiag(): String

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

    /**
     * Every setting of a native module, one line each, tab-separated:
     *     key \t label \t kind \t value \t min \t max \t options \t tooltip
     * kind is b|i|f|e. options is comma-joined and only used by enums.
     *
     * The in-game panel needs this so a memory-backed module can be configured
     * on the device it runs on — the external app cannot do it for them.
     */
    external fun nativeListSettings(module: String): String

    /**
     * Apply one setting. Send all three values; the SETTING's native type decides
     * which one is used, so the UI cannot desync from the module's real type.
     * Returns false when the module/key is unknown or the value is rejected.
     */
    external fun nativeSetSetting(module: String, key: String, f: Float, b: Boolean, i: Int): Boolean

    // ── PvP bot data plane (r27) ──────────────────────────────────────────
    //
    // DRIVEN BY YOU, NOT THE UI: these answer `botScan`-style questions about
    // the world using the SAME validated offsets and the SAME actor filter as
    // KillAura2 (melody::botScan), so the bot can never act on an actor the
    // module would itself refuse to attack. All of them fail inert: -1 / false
    // means "world not readable right now", never a guessed value.
    //
    // botScan packs 11 floats per actor, nearest first:
    //   [0..2] position    [3..5] velocity   [6] distance
    //   [7] health         [8] type id (approx.)  [9] flags (bit0 = damaged
    //   within the last 10 ticks)
    //   [10] entity id, exact — read it with Float.fromBits(v.toInt()).
    //        Indexes reshuffle between ticks, the entity id does not, so hit
    //        attribution must key on the id, never on the index.
    // mode: 0 = players only (strict), 1 = players only with automatic fallback
    // when the filter matches nobody (the fallback is reported), 2 = anything
    // attackable. Returns the actor count, or -1 when the world is not readable.
    external fun botScan(out: FloatArray, maxRange: Float, mode: Int): Int

    /**
     * Funnel of the last scan — actors seen, how many were attackable, which
     * stage rejected the rest, the sampled type ids and whether they came from
     * the ECS component or the game function. This is the first thing to read
     * when the bot "sees nobody".
     */
    external fun botScanDiag(): String

    /** Attack the actor at `index` of the most recent scan (same tick only). */
    external fun botAttack(index: Int): Boolean

    /** Yaw/pitch (degrees) that faces the actor at `index`; out[0]=yaw, out[1]=pitch. */
    external fun botAim(index: Int, out: FloatArray): Boolean

    /**
     * WALK — the game's own movement, not a velocity hack.
     *
     * Writes the same input state the on-screen joystick writes (the
     * MoveInputComponent element) and lets the engine do everything else:
     * speed, friction, collision, sprint rules, animations, footsteps.
     * dirX/dirZ are WORLD space and get rotated into the camera-relative input
     * axes with the player's current yaw.
     *
     * Returns false while the input element layout is not derived yet — the
     * deliberate failure mode: the bot then does not move at all instead of
     * moving badly. See botWalkStatus() for what is missing.
     */
    external fun botWalk(dirX: Float, dirZ: Float, jump: Boolean, sneak: Boolean): Boolean

    /**
     * Short, honest description of what walking is waiting for, e.g.
     * "walk: pool OK, element layout not derived". Call during a tick.
     */
    external fun botWalkStatus(): String

    /**
     * Self state: [0..2] pos, [3..5] velocity, [6] health, [7] damaged within
     * 10 ticks (1/0), [8] damage events in the last 5 s, [9] ticks since the
     * last damage event (-1 = never). out[8]/[9] drive the escalation rule.
     */
    external fun botSelf(out: FloatArray): Boolean

    /**
     * Whether aim can actually be APPLIED yet. False today: the rotation
     * component is not derived, so the bot computes the aim it needs and shows
     * it, but the player still has to look. Never pretend otherwise.
     */
    external fun botRotationAvailable(): Boolean

    // ── logcat, tag EClientKotlin (also tee'd to Download/EClient/*.txt) ──

    external fun logLine(line: String)
}
