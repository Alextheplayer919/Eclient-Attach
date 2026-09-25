package com.rubidiumclient.modules

/**
 * ═══ E-Client module framework (Kotlin-first, module-manager lives HERE) ═══
 *
 * Every user-facing module is a [KotlinModule] with self-declared [KSetting]
 * entries. The native C++ side is now only the engine: tick hook, memory
 * primitives (NativeBridge), patch profile, and one JNI dispatch per tick.
 * The in-game UI (menu + fly buttons) is the Android-Views overlay in
 * ui/EClientUi.kt — no ImGui, no GL interference (big FPS win).
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

    /** Self-declared settings, rendered as sliders/toggles/cyclers in the UI. */
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

    /** Optional verbose hint shown under the module in the UI. */
    open fun statusHint(): String? = null

    // Categories that can ever be held via the GUI fly buttons (drives the
    // ▲/▼ button visibility in the overlay).
    open val usesFlyButtons: Boolean get() = false

    open fun onEnable() {}
    open fun onDisable() {}

    /** Called once per game tick while enabled, on the game thread. */
    abstract fun onTick()

    protected fun log(line: String) = NativeBridge.logLine("$name: $line")
}
