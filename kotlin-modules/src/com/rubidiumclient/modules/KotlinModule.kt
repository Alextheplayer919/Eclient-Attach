package com.rubidiumclient.modules

/**
 * Base class for modules written in Kotlin that ship inside the patched APK
 * (dex → classes2.dex). They appear in the same E-Client menu as the native
 * C++ modules: the native ModuleManager owns the toggle and mirrors it into
 * [isEnabled]; the native tick dispatches [onTick] on the game thread.
 *
 * Context services (player position/velocity, the ▲/▼ buttons, settings)
 * come from [NativeBridge] and use the same validated 1.21.111 offsets as
 * the C++ modules.
 */
abstract class KotlinModule(
    val name: String,
    val category: String,
    val description: String,
) {
    @Volatile
    var isEnabled: Boolean = false

    open fun onEnable() {}
    open fun onDisable() {}

    /** Called once per game tick while enabled, on the game's tick thread. */
    abstract fun onTick()

    // Setting values live in the native ModuleManager (one menu).
    protected fun floatSetting(key: String, fallback: Float): Float =
        NativeBridge.getFloatSetting(name, key, fallback)

    protected fun boolSetting(key: String, fallback: Boolean): Boolean =
        NativeBridge.getBoolSetting(name, key, fallback)

    protected fun log(line: String) = NativeBridge.logLine("$name: $line")
}
