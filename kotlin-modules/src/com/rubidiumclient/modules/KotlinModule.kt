package com.rubidiumclient.modules

/**
 * ═══ E-Client module framework (Kotlin-first, module manager lives HERE) ═══
 *
 * Every user-facing module is a [KotlinModule] with self-declared [KSetting]
 * entries. The native C++ side is the engine only: tick hook, memory
 * primitives (NativeBridge), the patch profile, and one JNI dispatch per tick.
 * The in-game UI is the Android-Views overlay in ui/EClientUi.kt.
 *
 * r25 scope: PvP only. Fly/MotionFly and the visual patch modules were removed
 * (the C++ ModuleManager owns what remains of the native side); the fly-button
 * plumbing went with them.
 */

// ─── Settings ───────────────────────────────────────────────────────────────

sealed class KSetting(val key: String, val label: String, val desc: String)

class FloatSetting(key: String, label: String, val def: Float, val min: Float,
                   val max: Float, desc: String = "") : KSetting(key, label, desc) {
    @Volatile var value: Float = def
}

class IntSetting(key: String, label: String, val def: Int, val min: Int,
                 val max: Int, desc: String = "") : KSetting(key, label, desc) {
    @Volatile var value: Int = def
}

class BoolSetting(key: String, label: String, val def: Boolean,
                  desc: String = "") : KSetting(key, label, desc) {
    @Volatile var value: Boolean = def
}

class EnumSetting(key: String, label: String, val options: Array<String>,
                  val def: Int, desc: String = "") : KSetting(key, label, desc) {
    @Volatile var index: Int = def
    val value: String get() = options[index]
}

// ─── Module base ────────────────────────────────────────────────────────────

abstract class KotlinModule(
    val name: String,
    val category: String,   // Combat | Movement | Visual | Player | Misc
    val description: String,
) {
    @Volatile var isEnabled: Boolean = false

    /** Self-declared settings, rendered under the module in the UI. */
    val settings = mutableListOf<KSetting>()

    protected fun float(key: String, label: String, def: Float, min: Float,
                        max: Float, desc: String = "") =
        FloatSetting(key, label, def, min, max, desc).also { settings += it }

    protected fun int(key: String, label: String, def: Int, min: Int,
                      max: Int, desc: String = "") =
        IntSetting(key, label, def, min, max, desc).also { settings += it }

    protected fun bool(key: String, label: String, def: Boolean, desc: String = "") =
        BoolSetting(key, label, def, desc).also { settings += it }

    protected fun enum(key: String, label: String, options: Array<String>,
                       def: Int, desc: String = "") =
        EnumSetting(key, label, options, def, desc).also { settings += it }

    /**
     * Optional one-line truth shown under the module in the UI. Modules whose
     * memory is not derived yet use this to say so out loud — a half-finished
     * feature must be visibly pending, never silently broken.
     */
    open fun statusHint(): String? = null

    open fun onEnable() {}
    open fun onDisable() {}

    /** Called once per game tick while enabled, on the game thread. */
    abstract fun onTick()

    protected fun log(line: String) = NativeBridge.logLine("$name: $line")
}
