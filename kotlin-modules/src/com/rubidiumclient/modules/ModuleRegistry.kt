package com.rubidiumclient.modules

import android.content.Context
import com.rubidiumclient.modules.ui.EClientUi
import java.util.concurrent.CopyOnWriteArrayList

/**
 * The module manager — rewritten from scratch and living entirely in Kotlin.
 *
 * Entry points:
 *  - EClientLoaderProvider.onActivityResumed → EClientUi.attach(activity)
 *    (which calls [bind] with a Context) + [init]
 *  - libeclient_host game tick → [tick] (already JNI-bound class/method name)
 *
 * Responsibilities: registration, enable/disable + exclusivity groups,
 * SharedPreferences persistence, and the per-tick dispatch. The UI observes
 * this object; native C++ only calls [init]/[tick]/[setEnabled] (kept JNI
 * signature compatible).
 */
object ModuleRegistry {
    private val modules = CopyOnWriteArrayList<KotlinModule>()
    @Volatile private var appContext: Context? = null
    @Volatile private var prefsLoaded = false

    // Modules that may never be enabled at the same time (both drive the
    // velocity field + the ▲/▼ buttons).
    private val exclusivePairs = listOf("Fly" to "MotionFly")

    @Volatile var onModulesChanged: (() -> Unit)? = null

    @JvmStatic
    fun init() {
        if (modules.isEmpty()) {
            modules += Fly()
            modules += MotionFly()
            modules += FullBright()
            modules += Hitbox()
            modules += AutoTotem()
            NativeBridge.logLine("Kotlin module manager ready: " +
                modules.joinToString { it.name })
        }
    }

    /** Called with the app Context once the UI attaches. */
    fun bind(context: Context) {
        appContext = context.applicationContext
        loadPrefs()
    }

    fun all(): List<KotlinModule> = modules

    fun find(name: String): KotlinModule? = modules.firstOrNull { it.name == name }

    private fun notifyChanged() {
        try { onModulesChanged?.invoke() } catch (_: Throwable) {}
    }

    @JvmStatic
    fun setEnabled(name: String, enabled: Boolean) {
        val m = find(name) ?: return
        setModuleEnabled(m, enabled)
        notifyChanged()
    }

    fun setModuleEnabled(m: KotlinModule, enabled: Boolean) {
        if (m.isEnabled == enabled) return
        if (enabled) {
            for ((a, b) in exclusivePairs) {
                val otherName = if (m.name == a) b else if (m.name == b) a else null
                otherName?.let { nm -> find(nm)?.let { om -> setModuleEnabled(om, false) } }
            }
        }
        m.isEnabled = enabled
        try {
            if (enabled) m.onEnable() else m.onDisable()
        } catch (t: Throwable) {
            NativeBridge.logLine("${m.name} lifecycle error: $t")
        }
        if (!enabled && m.usesFlyButtons) EClientUi.setFlyButtonsVisible(anyFlyButtonsVisible())
        if (enabled && m.usesFlyButtons) EClientUi.setFlyButtonsVisible(true)
        savePrefs()
    }

    /** Fly buttons shown when any fly-button module is on AND wants them. */
    fun anyFlyButtonsVisible(): Boolean =
        modules.any { it.isEnabled && it.usesFlyButtons }

    // ── Tick (called from native, game thread) ──────────────────────────

    @JvmStatic
    fun tick() {
        for (m in modules) {
            if (!m.isEnabled) continue
            try {
                m.onTick()
            } catch (t: Throwable) {
                NativeBridge.logLine("tick error in ${m.name}: $t")
            }
        }
    }

    @JvmStatic
    fun tickAll() = tick() // keeps older/native call shapes working

    // ── Persistence ─────────────────────────────────────────────────────

    private fun prefs() = appContext?.getSharedPreferences("eclient_modules", 0)

    fun savePrefs() {
        val p = prefs() ?: return
        val e = p.edit()
        for (m in modules) {
            e.putBoolean("${m.name}.enabled", m.isEnabled)
            for (s in m.settings) {
                val k = "${m.name}.${s.key}"
                when (s) {
                    is BoolSetting -> e.putBoolean(k, s.value)
                    is IntSetting -> e.putInt(k, s.value)
                    is FloatSetting -> e.putFloat(k, s.value)
                    is EnumSetting -> e.putInt(k, s.index)
                }
            }
        }
        e.apply()
    }

    private fun loadPrefs() {
        if (prefsLoaded) return
        prefsLoaded = true
        val p = prefs() ?: return
        for (m in modules) {
            for (s in m.settings) {
                val k = "${m.name}.${s.key}"
                when (s) {
                    is BoolSetting -> s.value = p.getBoolean(k, s.def)
                    is IntSetting -> s.value = p.getInt(k, s.def)
                    is FloatSetting -> s.value = p.getFloat(k, s.def)
                    is EnumSetting -> s.index = p.getInt(k, s.def)
                }
            }
            if (p.getBoolean("${m.name}.enabled", false)) {
                // Same exclusive-group + lifecycle rules as a fresh toggle.
                setModuleEnabled(m, true)
            }
        }
        EClientUi.setFlyButtonsVisible(anyFlyButtonsVisible())
    }
}
