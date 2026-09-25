package com.rubidiumclient.modules

import java.util.concurrent.CopyOnWriteArrayList

/**
 * Entry point for the Kotlin module layer, invoked from the injected host
 * loader after the native library registers its JNI functions:
 *
 *   EClientLoaderProvider.onActivityResumed → ModuleRegistry.init()
 *
 * The native side mirrors menu toggles through [setEnabled] and drives
 * [tick] once per game tick from libeclient_host.
 *
 * To add a module: `modules += MyModule()` in [init].
 */
object ModuleRegistry {
    private val modules = CopyOnWriteArrayList<KotlinModule>()

    @JvmStatic
    fun init() {
        if (modules.isEmpty()) {
            modules += MotionFly()
            NativeBridge.logLine("Kotlin module layer ready: ${modules.size} module(s)")
        }
    }

    /** Calls from native EVERY enable-flip of a Kotlin module in the menu. */
    @JvmStatic
    fun setEnabled(name: String, enabled: Boolean) {
        val m = modules.firstOrNull { it.name == name } ?: return
        if (m.isEnabled == enabled) return
        m.isEnabled = enabled
        if (enabled) m.onEnable() else m.onDisable()
    }

    /** Calls from native once per game tick (game thread). */
    @JvmStatic
    fun tick() {
        for (m in modules) {
            if (!m.isEnabled) continue
            try {
                m.onTick()
            } catch (t: Throwable) {
                // Never let a Kotlin module crash the game's tick thread.
                NativeBridge.logLine("tick error in ${'$'}{m.name}: ${'$'}t")
            }
        }
    }
}
