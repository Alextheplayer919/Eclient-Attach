package com.rubidiumclient.modules

import android.content.Context
import java.util.concurrent.CopyOnWriteArrayList

/**
 * ═══ E-Client Kotlin module manager (PvP build, r25) ═══
 *
 * JNI CONTRACT (must not change — libeclient_host.so binds these by name):
 *   ModuleRegistry.tick()V                      ← native tick dispatch
 *   ModuleRegistry.setEnabled(Ljava/lang/String;Z)V
 *
 * The native side mirrors its own toggles in via setEnabled(); the UI calls
 * setModuleEnabled() directly. Both funnel into the same code path.
 *
 * SCOPE (r25): PvP only. The C++ Fly / MotionFly modules and the visual patch
 * modules were removed from the product; anything left here must earn its place.
 *
 * Prefs schema v2 wipes module state written by older builds — otherwise stale
 * "Fly.enabled=true" style keys resurrect modules that no longer exist.
 */
object ModuleRegistry {

    private const val PREFS_NAME = "eclient_modules"
    private const val SCHEMA_KEY = "schema"
    private const val SCHEMA = 2

    private val modules = CopyOnWriteArrayList<KotlinModule>()

    @Volatile private var appContext: Context? = null
    @Volatile private var prefsLoaded = false

    /** UI hook: the panel rebuilds itself while open when this fires. */
    @Volatile var onModulesChanged: (() -> Unit)? = null

    // ── registry ──────────────────────────────────────────────────────────

    @JvmStatic
    fun init() {
        if (modules.isNotEmpty()) return
        modules += Hitbox()
        modules += AutoTotem()
        modules += PvpBot()
        NativeBridge.logLine("ModuleRegistry ready: " + modules.joinToString { it.name })
    }

    /** Called with the activity once the UI attaches (also loads prefs). */
    fun bind(context: Context) {
        appContext = context.applicationContext
        loadPrefs()
    }

    fun all(): List<KotlinModule> = modules

    /** App-level prefs for a module that keeps its own state (e.g. the bot's
     *  learned scores). Returns null before bind(). */
    fun appPrefs(name: String): android.content.SharedPreferences? =
        appContext?.getSharedPreferences(name, 0)

    fun find(name: String): KotlinModule? = modules.firstOrNull { it.name == name }

    // ── enabling ──────────────────────────────────────────────────────────

    /** JNI entry point (native calls this with a module name). */
    @JvmStatic
    fun setEnabled(name: String, enabled: Boolean) {
        val m = find(name) ?: return
        setModuleEnabled(m, enabled)
    }

    fun setModuleEnabled(m: KotlinModule, enabled: Boolean) {
        if (m.isEnabled == enabled) return

        // NOTE: the Fly ⇄ MotionFly exclusivity pair that used to live here is
        // gone with those modules. If two future modules must never run at the
        // same time, re-add the pair here AND in the native ModuleManager.
        m.isEnabled = enabled
        try {
            if (enabled) m.onEnable() else m.onDisable()
        } catch (t: Throwable) {
            NativeBridge.logLine("${m.name} lifecycle error: $t")
        }

        pushTickState()
        savePrefs()
        try {
            onModulesChanged?.invoke()
        } catch (_: Throwable) {
        }
    }

    /**
     * The native tick gate only dispatches Kotlin ticks while at least one
     * Kotlin module is on (one JNI call per game tick otherwise wasted).
     * Before r25 this was inferred from a duplicated native mirror entry —
     * which is exactly how the two registries drifted apart.
     */
    private fun pushTickState() {
        try {
            NativeBridge.setKotlinTickActive(modules.any { it.isEnabled })
        } catch (_: Throwable) {
        }
    }

    // ── tick (called from native, game thread) ────────────────────────────

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

    /** Kept for older call shapes. */
    @JvmStatic
    fun tickAll() = tick()

    // ── persistence ───────────────────────────────────────────────────────

    private fun prefs() = appContext?.getSharedPreferences(PREFS_NAME, 0)

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
        e.putInt(SCHEMA_KEY, SCHEMA)
        e.apply()
    }

    private fun loadPrefs() {
        if (prefsLoaded) return
        prefsLoaded = true
        val p = prefs() ?: return

        // Schema migration: state written by a build with a different module set
        // must never be replayed onto this one.
        if (p.getInt(SCHEMA_KEY, 0) < SCHEMA) {
            p.edit().clear().putInt(SCHEMA_KEY, SCHEMA).apply()
            NativeBridge.logLine("ModuleRegistry: prefs schema wiped to v$SCHEMA")
            pushTickState()
            return
        }

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
                m.isEnabled = true
                try {
                    m.onEnable()
                } catch (t: Throwable) {
                    NativeBridge.logLine("${m.name} restore error: $t")
                }
            }
        }
        pushTickState()
    }
}
