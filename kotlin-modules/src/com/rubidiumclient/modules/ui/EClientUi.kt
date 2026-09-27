package com.rubidiumclient.modules.ui

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.rubidiumclient.modules.BoolSetting
import com.rubidiumclient.modules.EnumSetting
import com.rubidiumclient.modules.FloatSetting
import com.rubidiumclient.modules.IntSetting
import com.rubidiumclient.modules.KSetting
import com.rubidiumclient.modules.KotlinModule
import com.rubidiumclient.modules.ModuleRegistry
import com.rubidiumclient.modules.NativeBridge
import java.io.OutputStream

/**
 * ═══ E-Client overlay — r26 ═══
 *
 * WHY r26 EXISTS
 *
 * r25 shipped a correctly-structured overlay that showed nothing on the device,
 * and the failure was invisible: every mount step was wrapped in a try/catch
 * that only wrote to logcat, and *attach() itself produced no user-visible
 * signal at all*. So "the code never ran", "addView threw" and "the window was
 * added but is not visible" all looked identical from the outside — nothing.
 *
 * r26 removes that whole class of ambiguity:
 *
 *   1. The chip is mounted BEFORE anything else that can fail (module registry,
 *      prefs, JNI). If every other subsystem is broken, the chip still appears.
 *   2. Mounting walks a LADDER of mechanisms and reports which one won:
 *        PANEL     — TYPE_APPLICATION_PANEL sub-window on the activity token
 *                    (proven on the device in r24; input passes through)
 *        INJECTED  — a plain child view of the game's own content frame. Cannot
 *                    fail for token/permission/z-order reasons; the game's
 *                    SurfaceView composites *behind* its window's views, so a
 *                    view added here is always above the game.
 *        OVERLAY   — TYPE_APPLICATION_OVERLAY window (needs "Display over other
 *                    apps"; offered as a one-tap button, never forced)
 *      The winner is written into the chip label itself ("r26 · PANEL"), so a
 *      screenshot of the chip is a complete bug report.
 *   3. Three independent proof-of-life signals, each proving a different layer:
 *        native toast  ← C++ (libeclient_host.so loaded, JNI fine)          [KotlinBridge]
 *        Kotlin toast  ← injected dex runs (classes6.dex classloader fine)
 *        chip label    ← the window/view layer works
 *      Seeing which of the three is missing localises the fault instantly.
 *   4. A persistent log file (`Download/EClient/eclient_ui*.txt`, plus the
 *      app-private external dir) records build tag, every mount attempt with
 *      its exception, watchdog events and touch counts — retrievable without
 *      adb.
 *
 * Everything r25 fixed is kept, because it is still correct:
 *   * nothing is ever MATCH_PARENT and nothing is ever window-modal, so a
 *     leaked menu can only ever eat its own ~320dp rectangle, never the screen;
 *   * attach() always rebuilds (no early return over stale references);
 *   * a 1 Hz watchdog re-mounts a chip the framework tore down;
 *   * close paths are explicit only (✕ / chip / long-press panic) — there is
 *     deliberately no tap-outside-to-close, because that is what forced the old
 *     design to put a full-screen scrim on top of the game;
 *   * the decor-view OnTouchListener is NOT installed: nothing of ours sits in
 *     the game's own input dispatch path.
 *
 * ABI: `attach(Landroid/app/Activity;)V` is invoked by name from the loader
 * smali and must keep exactly this name and signature.
 */
@SuppressLint("ClickableViewAccessibility")
/** Logs a line at most once per JVM, so headless mode does not spam logcat. */
internal object DiagOnce {
    private val seen = java.util.concurrent.ConcurrentHashMap<String, Boolean>()
    fun headlessLogged(tag: String, msg: String) {
        if (seen.putIfAbsent(tag + msg, true) == null) {
            android.util.Log.i(tag, msg)
        }
    }
}

object EClientUi {

    const val BUILD_TAG = "r42"
    private const val TAG = "EClientUi"
    private const val LOG_BASE = "eclient_ui"

    // ── palette ───────────────────────────────────────────────────────────
    private const val COL_CHIP_BG = 0xE6141A22.toInt()
    private const val COL_PANEL_BG = 0xF210141A.toInt()
    private const val COL_CARD_BG = 0xFF1A222C.toInt()
    private const val COL_TEXT = 0xFFD7DEE7.toInt()
    private const val COL_DIM = 0xFF8A96A3.toInt()
    private const val COL_ACCENT = 0xFF4C9AFF.toInt()
    private const val COL_GOOD = 0xFF3FB950.toInt()
    private const val COL_WARN = 0xFFE3B341.toInt()

    private val DEV_ONLY = setOf("ECSDump")

    private val main = Handler(Looper.getMainLooper())

    /**
     * How the widgets got onto the screen. Reported in the chip label, the panel
     * status line, the log file and the toast — the single most useful fact when
     * a device behaves differently from the emulator.
     */
    private enum class Strategy(val tag: String) { NONE("none"), PANEL("PANEL"), INJECTED("INJECTED"), OVERLAY("OVERLAY") }

    @Volatile private var wm: WindowManager? = null
    @Volatile private var activity: Activity? = null

    @Volatile private var chip: TextView? = null
    @Volatile private var strategy: Strategy = Strategy.NONE
    @Volatile private var attempts = ""
    @Volatile private var chipX = 0
    @Volatile private var chipY = 0

    /** Set when a widget lives as a plain child of the game's content frame. */
    private var chipParent: FrameLayout? = null
    private var panelParent: FrameLayout? = null

    @Volatile private var panel: View? = null
    @Volatile private var panelOpen = false
    @Volatile private var devMode = false

    // ── on-screen diagnostics ─────────────────────────────────────────────
    @Volatile private var panelTouches = 0
    @Volatile private var chipTouches = 0
    @Volatile private var lastTouch = "-"
    @Volatile private var statusLine: TextView? = null
    @Volatile private var beat = false

    // ── persistent log ────────────────────────────────────────────────────
    private val logLock = Any()
    private var logOut: OutputStream? = null
    private var logName: String = "$LOG_BASE.log"

    // ══════════════════════════════════════════════════════════════════════
    // Lifecycle — the only two public entry points (ABI)
    // ══════════════════════════════════════════════════════════════════════

    /**
     * VISIBLE BY DEFAULT (r42).
     *
     * This is THE clickgui, and it is the only one that can be. The companion
     * app runs in a different process and cannot touch Minecraft's memory at
     * all — it only receives numbers the in-game agent already read (see
     * docs/DO_NOT_BREAK.md, "the app cannot read memory"). Every control that
     * changes game memory has to be driven from in here: KillAura2's range and
     * rotation mode, Fly's mode, and the settings behind the patch layer.
     *
     * r37 made this headless because the external app could drive the module
     * toggles over the agent bridge. That is true for toggles and false for
     * everything that needs the memory layer, so the UI is on by default again.
     *
     * To hide it anyway (a clean session, a recording, a server that checks
     * screenshots): create /sdcard/Download/eclient_ui_off and relaunch the
     * game. The ABI is unchanged either way — the loader smali still calls
     * `attach(Activity)` by name and signature.
     */
    @JvmStatic
    fun attach(act: Activity) {
        if (!uiEnabled()) {
            DiagOnce.headlessLogged("EClientUi", "headless: in-game UI disabled by eclient_ui_off")
            return
        }
        if (Looper.myLooper() == Looper.getMainLooper()) attachOnMain(act)
        else main.post { attachOnMain(act) }
    }

    /** True unless the opt-out flag file exists. Cheap; called on every attach. */
    private fun uiEnabled(): Boolean {
        if (forcedUi != null) return forcedUi!!
        val off = try {
            java.io.File("/sdcard/Download/eclient_ui_off").exists() ||
                java.io.File("/sdcard/eclient_ui_off").exists()
        } catch (_: Throwable) { false }
        forcedUi = !off
        return forcedUi!!
    }

    /** Cached flag, so the check costs one stat per process. */
    @Volatile private var forcedUi: Boolean? = null

    @JvmStatic
    fun detach() {
        if (Looper.myLooper() == Looper.getMainLooper()) detachOnMain()
        else main.post { detachOnMain() }
    }

    private fun attachOnMain(act: Activity) {
        // ★ The chip is the product. Everything else is optional, so it goes
        //   first and its own failures never abort the attach.
        activity = act
        wm = act.windowManager
        openLog(act)
        log("attach ← ${act.javaClass.name}")

        teardownWidgets()
        mountChip()

        // ★ Only now the optional subsystems, each isolated.
        try {
            ModuleRegistry.init()
        } catch (t: Throwable) {
            log("ModuleRegistry.init failed: $t")
        }
        try {
            ModuleRegistry.bind(act)
        } catch (t: Throwable) {
            log("ModuleRegistry.bind failed: $t")
        }
        try {
            ModuleRegistry.onModulesChanged = { rebuildPanel() }
        } catch (t: Throwable) {
            log("onModulesChanged hook failed: $t")
        }

        startWatchdog()

        val ok = strategy != Strategy.NONE
        toast(
            if (ok) "E-Client $BUILD_TAG: menu up ($strategy.tag)"
            else "E-Client $BUILD_TAG: menu FAILED — log in Download/EClient"
        )
        log("attach done: strategy=${strategy.tag} chip=${chip != null} attempts=[$attempts]")
    }

    private fun detachOnMain() {
        main.removeCallbacks(watchdog)
        teardownWidgets()
        closeLog()
        activity = null
        log("detached")
    }

    // ══════════════════════════════════════════════════════════════════════
    // Mounting — the ladder that makes "nothing appears" impossible to reach
    // ══════════════════════════════════════════════════════════════════════

    private fun mountChip() {
        attempts = ""
        val v = try {
            buildChip()
        } catch (t: Throwable) {
            log("buildChip failed: $t")
            attempts += "build: $t; "
            return
        }

        // 1) PANEL — the mechanism r24 proved on this device class.
        if (addWindow(v, panelLp(chipX, chipY, wrap = true))) {
            chip = v; chipParent = null; strategy = Strategy.PANEL
            refreshChipLabel()
            return
        }
        // 2) INJECTED — cannot fail for window/token/z-order reasons.
        if (inject(v, dp(112), dp(36))) {
            chip = v; strategy = Strategy.INJECTED
            refreshChipLabel()
            return
        }
        // 3) OVERLAY — needs the permission; may be denied.
        if (canOverlay() && addWindow(v, overlayLp(chipX, chipY, wrap = true))) {
            chip = v; chipParent = null; strategy = Strategy.OVERLAY
            refreshChipLabel()
            return
        }
        strategy = Strategy.NONE
        log("mountChip: every strategy failed — $attempts")
    }

    /** Keep the chip label honest: build tag + live strategy + heartbeat. */
    private fun refreshChipLabel() {
        chip?.text = chipLabel()
    }

    /** Attach a view as a WindowManager sub-window. False = this rung failed. */
    private fun addWindow(v: View, lp: WindowManager.LayoutParams?): Boolean {
        val w = wm ?: run { attempts += "no WindowManager; "; return false }
        if (lp == null) { attempts += "no LayoutParams; "; return false }
        return try {
            w.addView(v, lp)
            log("window +${v.javaClass.simpleName} type=${lp.type} (${lp.width}x${lp.height})")
            true
        } catch (t: Throwable) {
            attempts += "addView(type=${lp.type}): $t; "
            log("addView failed (type=${lp.type}): $t")
            // A half-added view would keep the reference alive forever — drop it.
            try { if (v.parent != null) w.removeViewImmediate(v) } catch (_: Throwable) {}
            false
        }
    }

    /**
     * Attach a view as a child of the game's own content frame.
     *
     * This is the last rung and the reason "nothing shows" is now impossible:
     * it needs no token, no permission and no z-order, and the game's
     * SurfaceView composites behind its window's view hierarchy, so anything
     * added here draws on top of the game.
     */
    private fun inject(v: View, width: Int, height: Int): Boolean {
        val act = activity ?: run { attempts += "no activity; "; return false }
        return try {
            val parent = contentFrame(act)
            if (parent == null) { attempts += "no content frame; "; return false }
            val lp = FrameLayout.LayoutParams(width, height).apply {
                gravity = Gravity.TOP or Gravity.START
                leftMargin = chipX
                topMargin = chipY
            }
            parent.addView(v, lp)
            if (v === chip) chipParent = parent else panelParent = parent
            log("injected into content frame ($width x $height)")
            true
        } catch (t: Throwable) {
            attempts += "inject: $t; "
            log("inject failed: $t")
            false
        }
    }

    private fun contentFrame(act: Activity): FrameLayout? {
        val byId = act.findViewById<ViewGroup>(android.R.id.content)
        if (byId is FrameLayout) return byId
        val decor = act.window?.decorView
        if (decor is FrameLayout) return decor
        return null
    }

    private fun teardownWidgets() {
        removePanelNow()
        val c = chip
        chip = null
        chipParent = null
        if (c != null) removeViewFromWhereverItIs(c)
        strategy = Strategy.NONE
    }

    private fun removeViewFromWhereverItIs(v: View) {
        try {
            val p = v.parent
            when {
                p is FrameLayout -> p.removeView(v)          // injected
                p != null -> wm?.removeViewImmediate(v)       // sub-window / overlay
            }
        } catch (t: Throwable) {
            log("remove view: $t")
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // Window recipes — the input guarantees live here
    // ══════════════════════════════════════════════════════════════════════

    /**
     * FLAG_NOT_FOCUSABLE   keys/back stay with the game; we never take focus.
     * FLAG_NOT_TOUCH_MODAL everything not landing on this widget goes to the game.
     *
     * `wrap = true` is the chip: WRAP_CONTENT, its own view size decides.
     * Never MATCH_PARENT — that is the r24 input bug, by construction.
     */
    private fun baseFlags() =
        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL

    private fun panelLp(x: Int, y: Int, wrap: Boolean): WindowManager.LayoutParams? {
        val a = activity ?: return null
        val token = try { a.window?.attributes?.token } catch (_: Throwable) { null }
        return WindowManager.LayoutParams(
            if (wrap) ViewGroup.LayoutParams.WRAP_CONTENT else dp(320),
            ViewGroup.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            baseFlags(),
            PixelFormat.TRANSLUCENT
        ).apply {
            this.token = token
            gravity = Gravity.TOP or Gravity.START
            this.x = x
            this.y = y
            if (!wrap) this.height = (screenH() * 0.62f).toInt().coerceAtMost(dp(560))
        }
    }

    private fun overlayLp(x: Int, y: Int, wrap: Boolean): WindowManager.LayoutParams? {
        @Suppress("DEPRECATION")
        return WindowManager.LayoutParams(
            if (wrap) ViewGroup.LayoutParams.WRAP_CONTENT else dp(320),
            ViewGroup.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            baseFlags(),
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            this.x = x
            this.y = y
            if (!wrap) this.height = (screenH() * 0.62f).toInt().coerceAtMost(dp(560))
        }
    }

    private fun canOverlay(): Boolean = try {
        activity?.let { Settings.canDrawOverlays(it) } ?: false
    } catch (_: Throwable) {
        false
    }

    // ══════════════════════════════════════════════════════════════════════
    // The chip (always present while attached)
    // ══════════════════════════════════════════════════════════════════════

    private fun chipLabel(): String = "E-Client $BUILD_TAG · ${strategy.tag} ${if (beat) "•" else "○"}"

    private fun buildChip(): TextView {
        val v = TextView(actContext()).apply {
            text = chipLabel()
            textSize = 11f
            setTextColor(Color.WHITE)
            setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER
            background = rounded(COL_CHIP_BG, 12f)
            alpha = 0.92f
            layoutParams = ViewGroup.LayoutParams(dp(112), dp(34))
        }

        val p = uiPrefs()
        // Prefs written in another orientation can land off-screen; clamp so the
        // widget can never be mounted somewhere the user cannot see or reach.
        chipX = (p?.getInt("chip_x", dp(12)) ?: dp(12))
            .coerceIn(0, (screenW() - dp(112)).coerceAtLeast(0))
        chipY = (p?.getInt("chip_y", dp(48)) ?: dp(48))
            .coerceIn(0, (screenH() - dp(36)).coerceAtLeast(0))

        var x = chipX
        var y = chipY
        var downX = chipX
        var downY = chipY
        var downRawX = 0f
        var downRawY = 0f
        var moved = false
        var downAt = 0L

        v.setOnTouchListener { view, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = x; downY = y
                    downRawX = ev.rawX; downRawY = ev.rawY
                    moved = false
                    downAt = System.currentTimeMillis()
                    chipTouches++
                    view.alpha = 1f
                    refreshStatus()
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = (ev.rawX - downRawX).toInt()
                    val dy = (ev.rawY - downRawY).toInt()
                    if (kotlin.math.abs(dx) + kotlin.math.abs(dy) > dp(6)) moved = true
                    if (moved) {
                        x = (downX + dx).coerceIn(0, (screenW() - dp(112)).coerceAtLeast(0))
                        y = (downY + dy).coerceIn(0, (screenH() - dp(34)).coerceAtLeast(0))
                        moveChip(view, x, y)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    view.alpha = 0.92f
                    if (moved) {
                        uiPrefs()?.edit()?.putInt("chip_x", x)?.putInt("chip_y", y)?.apply()
                    } else if (ev.actionMasked == MotionEvent.ACTION_UP) {
                        if (System.currentTimeMillis() - downAt > 700L) panic()
                        else if (panelOpen) closePanel() else openPanel()
                    }
                    true
                }
                else -> false
            }
        }
        return v
    }

    private fun moveChip(v: View, x: Int, y: Int) {
        chipX = x
        chipY = y
        try {
            if (v.parent is FrameLayout) {
                val lp = (v.layoutParams as? FrameLayout.LayoutParams)
                    ?: FrameLayout.LayoutParams(dp(112), dp(36))
                lp.leftMargin = x
                lp.topMargin = y
                v.layoutParams = lp
                return
            }
            val winLp = WindowManager.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                if (strategy == Strategy.OVERLAY) WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                else WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                baseFlags(),
                PixelFormat.TRANSLUCENT
            ).apply {
                if (strategy == Strategy.PANEL) token = activity?.window?.attributes?.token
                gravity = Gravity.TOP or Gravity.START
                this.x = x
                this.y = y
            }
            wm?.updateViewLayout(v, winLp)
        } catch (t: Throwable) {
            log("moveChip: $t")
        }
    }

    private fun panic() {
        log("panic: overlay reset")
        try {
            closePanel()
            chipX = dp(12)
            chipY = dp(48)
            uiPrefs()?.edit()?.putInt("chip_x", chipX)?.putInt("chip_y", chipY)?.apply()
            chip?.let { moveChip(it, chipX, chipY) }
            toast("overlay reset")
        } catch (t: Throwable) {
            log("panic: $t")
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // The panel (exists only while open, never full-screen, never modal)
    // ══════════════════════════════════════════════════════════════════════

    private fun openPanel() {
        if (panel != null) return
        try {
            val body = buildPanel()
            val w = dp(320).coerceAtMost((screenW() * 0.86f).toInt())
            val h = (screenH() * 0.62f).toInt().coerceAtMost(dp(560))

            // Same ladder as the chip: whichever mechanism carried the chip
            // carries the panel — consistency matters more than preference.
            val mounted = when (strategy) {
                Strategy.INJECTED -> injectPanel(body, w, h)
                Strategy.OVERLAY -> addWindow(body, overlayLp(dp(12), dp(12), wrap = false))
                else -> addWindow(body, panelLp(dp(12), dp(12), wrap = false))
            }
            if (!mounted) log("openPanel: mount failed (strategy=${strategy.tag}) — $attempts")
            panel = body
            panelOpen = true
            log("panel opened ${w}x$h (strategy=${strategy.tag}, outside touches stay with the game)")
            refreshStatus()
        } catch (t: Throwable) {
            panel = null
            panelOpen = false
            log("openPanel failed: $t")
            toast("menu failed to open — see log")
        }
    }

    private fun injectPanel(v: View, w: Int, h: Int): Boolean {
        val parent = chipParent ?: activity?.let { contentFrame(it) } ?: return false
        return try {
            parent.addView(v, FrameLayout.LayoutParams(w, h).apply {
                gravity = Gravity.TOP or Gravity.START
                leftMargin = dp(12)
                topMargin = dp(12)
            })
            panelParent = parent
            true
        } catch (t: Throwable) {
            log("injectPanel: $t")
            false
        }
    }

    private fun closePanel() {
        // Removals triggered by a touch on our own widget must not happen inside
        // the dispatch pass — defer to the next loop turn.
        main.post { removePanelNow() }
    }

    private fun removePanelNow() {
        val p = panel
        panel = null
        panelOpen = false
        statusLine = null
        panelParent = null
        if (p == null) return
        removeViewFromWhereverItIs(p)
        log("panel closed")
    }

    private fun togglePanel() {
        if (panelOpen) closePanel() else openPanel()
    }

    // ══════════════════════════════════════════════════════════════════════
    // Watchdog — self-healing, and now also the heartbeat
    // ══════════════════════════════════════════════════════════════════════

    private val watchdog = object : Runnable {
        override fun run() {
            try {
                ensureMounted()
            } catch (t: Throwable) {
                log("watchdog: $t")
            }
            main.postDelayed(this, 1000L)
        }
    }

    private fun startWatchdog() {
        main.removeCallbacks(watchdog)
        main.postDelayed(watchdog, 1000L)
    }

    private fun ensureMounted() {
        if (activity == null) return
        val c = chip
        if (c == null) {
            log("watchdog: no chip → mounting")
            mountChip()
            return
        }
        if (!c.isAttachedToWindow) {
            log("watchdog: chip detached (strategy=${strategy.tag}) → remounting")
            teardownWidgets()
            mountChip()
            return
        }
        beat = !beat
        refreshChipLabel()
        refreshStatus()
    }

    // ══════════════════════════════════════════════════════════════════════
    // Panel content
    // ══════════════════════════════════════════════════════════════════════

    private fun refreshStatus() {
        statusLine?.text = buildStatusText()
    }

    private fun buildStatusText(): String {
        val patch = try {
            NativeBridge.nativePatchStatus()
        } catch (t: Throwable) {
            "patch: ? ($t)"
        }
        return "strategy=${strategy.tag}  touch #$panelTouches chip #$chipTouches last=$lastTouch\n$patch"
    }

    private fun buildPanel(): View {
        val root = TouchScroll(actContext()) { ev ->
            panelTouches++
            lastTouch = "${touchName(ev)} ${ev.x.toInt()},${ev.y.toInt()}"
            refreshStatus()
        }
        root.background = rounded(COL_PANEL_BG, 14f)
        root.setPadding(dp(12), dp(10), dp(12), dp(10))

        val col = LinearLayout(actContext()).apply { orientation = LinearLayout.VERTICAL }
        root.addView(col, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

        // header
        val head = LinearLayout(actContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val title = label("E-Client $BUILD_TAG", 16f, COL_TEXT, bold = true).apply {
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        if (Build.VERSION.SDK_INT >= 26) title.tooltipText = "long-press for dev mode"
        title.isClickable = true
        title.setOnLongClickListener {
            devMode = !devMode
            toast("dev mode ${if (devMode) "on" else "off"}")
            rebuildPanel()
            true
        }
        head.addView(title)
        head.addView(label("✕", 18f, COL_WARN, bold = true).apply {
            setPadding(dp(10), dp(2), dp(6), dp(4))
            isClickable = true
            setOnClickListener { closePanel() }
        })
        col.addView(head)

        col.addView(label("tap ✕ or the chip to close · outside = game", 10f, COL_DIM))

        statusLine = label(buildStatusText(), 10f, COL_ACCENT)
        col.addView(statusLine)
        col.addView(label("log: Download/EClient/eclient_ui*.txt", 9f, COL_DIM))

        if (!canOverlay()) {
            col.addView(label("grant 'display over other apps' (optional, most robust)", 10f, COL_WARN))
            col.addView(label("GRANT OVERLAY", 12f, COL_ACCENT, bold = true).apply {
                setPadding(dp(8), dp(6), dp(8), dp(6))
                background = rounded(COL_CARD_BG, 6f)
                isClickable = true
                setOnClickListener { askOverlayPermission() }
            })
        }

        val natives = nativeModules()
        if (natives.isNotEmpty()) {
            col.addView(sectionLabel("NATIVE"))
            for (m in natives) col.addView(nativeRow(m))
        }

        col.addView(sectionLabel("MODULES"))
        for (m in ModuleRegistry.all()) {
            if (!devMode && DEV_ONLY.contains(m.name)) continue
            col.addView(kotlinRow(m))
        }
        return root
    }

    private fun sectionLabel(text: String) = label(text, 11f, COL_DIM, bold = true).apply {
        setPadding(0, dp(8), 0, dp(2))
    }

    private fun askOverlayPermission() {
        try {
            val ctx = actContext()
            val i = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + ctx.packageName)
            ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            ctx.startActivity(i)
            toast("allow 'display over other apps', then reopen the menu")
        } catch (t: Throwable) {
            log("askOverlayPermission: $t")
        }
    }

    // ── rows ──────────────────────────────────────────────────────────────

    private class NativeRow(
        val name: String,
        val category: String,
        val enabled: Boolean,
        val memoryBacked: Boolean,
        val desc: String,
    )

    private fun nativeModules(): List<NativeRow> {
        val raw = try {
            NativeBridge.nativeListModules() ?: ""
        } catch (t: Throwable) {
            log("nativeListModules failed: $t")
            return emptyList()
        }
        val out = mutableListOf<NativeRow>()
        for (line in raw.split('\n')) {
            if (line.isBlank()) continue
            val f = line.split('\t')
            if (f.size < 5) continue
            if (!devMode && DEV_ONLY.contains(f[0])) continue
            out += NativeRow(f[0], f[1], f[2] == "1", f[3] == "1", f[4])
        }
        return out
    }

    /**
     * One setting of a native (C++) module, as sent by nativeListSettings().
     *
     * kind: 'b' bool, 'i' int, 'f' float, 'e' enum (v is the option index).
     * All values travel as a float so one parser covers every kind.
     */
    private class NativeSetting(
        val key: String,
        val label: String,
        val kind: Char,
        var v: Float,
        val vMin: Float,
        val vMax: Float,
        val options: List<String>,
        val desc: String,
    ) {
        val text: String
            get() = when (kind) {
                'b' -> if (v > 0.5f) "on" else "off"
                'e' -> options.getOrElse(v.toInt()) { "?" }
                'i' -> v.toInt().toString()
                else -> fmt(v)
            }
    }

    /** Empty list = the module has no settings, or native did not answer. */
    private fun nativeSettingsFor(module: String): List<NativeSetting> {
        val raw = try {
            NativeBridge.nativeListSettings(module) ?: ""
        } catch (t: Throwable) {
            log("nativeListSettings($module) failed: $t")
            return emptyList()
        }
        val out = mutableListOf<NativeSetting>()
        for (line in raw.split('\n')) {
            if (line.isBlank()) continue
            val f = line.split('\t')
            if (f.size < 8) continue
            out += NativeSetting(
                key     = f[0],
                label   = f[1],
                kind    = f[2].firstOrNull() ?: 'f',
                v       = f[3].toFloatOrNull() ?: 0f,
                vMin    = f[4].toFloatOrNull() ?: 0f,
                vMax    = f[5].toFloatOrNull() ?: 1f,
                options = if (f[6].isEmpty()) emptyList() else f[6].split(','),
                desc    = f[7],
            )
        }
        return out
    }

    /**
     * Push one value; the native side picks the field that matches its type.
     *
     * The module name is passed IN, not read from a field: several modules share
     * setting keys ("Mode" exists on both Fly and KillAura2), so a shared
     * "currently editing" field could write a value into the wrong module once a
     * rebuild swapped the rows underneath a control.
     */
    private fun applyNative(module: String, s: NativeSetting, value: Float): Boolean {
        val ok = try {
            NativeBridge.nativeSetSetting(module, s.key, value, value > 0.5f, value.toInt())
        } catch (t: Throwable) {
            log("nativeSetSetting($module.${s.key}) failed: $t")
            false
        }
        if (!ok) toast("${s.key}: refused")
        return ok
    }

    private fun nativeRow(m: NativeRow): View {
        val row = LinearLayout(actContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(4), 0, dp(4))
        }
        val texts = LinearLayout(actContext()).apply { orientation = LinearLayout.VERTICAL }
        texts.addView(label(m.name, 14f, COL_TEXT, bold = true))
        texts.addView(label("${m.category} · ${m.desc}", 11f, COL_DIM))
        if (m.memoryBacked) texts.addView(label("memory patch", 10f, COL_DIM))
        row.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))

        row.addView(Switch(actContext()).apply {
            isChecked = m.enabled
            setOnCheckedChangeListener { _, want ->
                val applied = try {
                    NativeBridge.nativeSetModuleEnabled(m.name, want)
                } catch (t: Throwable) {
                    log("setEnabled(${m.name}) failed: $t")
                    false
                }
                if (applied != want) {
                    isChecked = m.enabled
                    toast("${m.name}: refused (patch profile not validated)")
                } else {
                    rebuildPanel()   // show/hide this module's settings
                }
            }
        })

        // Settings, same rule as the Kotlin rows: shown while the module is on,
        // because that is when they do something. Values are re-read from native
        // on every rebuild, so the app and this panel never drift apart.
        val wrap = LinearLayout(actContext()).apply { orientation = LinearLayout.VERTICAL }
        wrap.addView(row)
        if (m.enabled) {
            val settings = nativeSettingsFor(m.name)
            if (settings.isNotEmpty()) {
                val col = LinearLayout(actContext()).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(dp(6), dp(2), 0, dp(2))
                }
                for (s in settings) col.addView(nativeSettingRow(m.name, s))
                wrap.addView(col)
            }
        }
        return wrap
    }

    /**
     * One native setting: Switch for bools, SeekBar for numbers, a cycling
     * button for enums — deliberately the same controls the Kotlin settings use,
     * so the panel reads as one list rather than two design languages.
     *
     * Every change is applied immediately (the JNI call is a mutex plus a
     * lookup, and seeing the effect while dragging is the point of having these
     * in-game at all).
     */
    private fun nativeSettingRow(module: String, s: NativeSetting): View {
        val row = LinearLayout(actContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(3), 0, dp(3))
        }
        row.addView(label(s.label, 12f, COL_TEXT).apply {
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            if (Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) tooltipText = s.desc
        })

        when (s.kind) {
            'b' -> row.addView(Switch(actContext()).apply {
                isChecked = s.v > 0.5f
                setOnCheckedChangeListener { _, want ->
                    val next = if (want) 1f else 0f
                    if (applyNative(module, s, next)) s.v = next else isChecked = s.v > 0.5f
                }
            })

            'e' -> row.addView(label(s.text, 12f, COL_ACCENT).apply {
                setPadding(dp(8), dp(4), dp(8), dp(4))
                background = rounded(COL_CARD_BG, 6f)
                isClickable = true
                setOnClickListener {
                    if (s.options.isEmpty()) return@setOnClickListener
                    val next = ((s.v.toInt() + 1) % s.options.size).toFloat()
                    if (applyNative(module, s, next)) {
                        s.v = next
                        text = s.text
                    }
                }
            })

            'i' -> {
                val tv = label(s.text, 11f, COL_ACCENT)
                row.addView(tv)
                val span = (s.vMax - s.vMin).coerceAtLeast(1f)
                row.addView(SeekBar(actContext()).apply {
                    max = span.toInt()
                    progress = (s.v - s.vMin).toInt().coerceIn(0, max)
                    layoutParams = LinearLayout.LayoutParams(dp(110), ViewGroup.LayoutParams.WRAP_CONTENT)
                        .apply { leftMargin = dp(6) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            val next = s.vMin + p
                            if (applyNative(module, s, next)) {
                                s.v = next
                                tv.text = s.text
                            }
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) {}
                    })
                })
            }

            else -> {   // 'f'
                val tv = label(s.text, 11f, COL_ACCENT)
                row.addView(tv)
                val span = (s.vMax - s.vMin).takeIf { it > 0f } ?: 1f
                row.addView(SeekBar(actContext()).apply {
                    max = 1000
                    progress = (((s.v - s.vMin) / span) * 1000f).toInt().coerceIn(0, 1000)
                    layoutParams = LinearLayout.LayoutParams(dp(110), ViewGroup.LayoutParams.WRAP_CONTENT)
                        .apply { leftMargin = dp(6) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            val next = s.vMin + (p / 1000f) * span
                            if (applyNative(module, s, next)) {
                                s.v = next
                                tv.text = s.text
                            }
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) {}
                    })
                })
            }
        }
        return row
    }

    private fun kotlinRow(m: KotlinModule): View {
        val wrap = LinearLayout(actContext()).apply { orientation = LinearLayout.VERTICAL }
        val row = LinearLayout(actContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(4), 0, dp(4))
        }
        val texts = LinearLayout(actContext()).apply { orientation = LinearLayout.VERTICAL }
        texts.addView(label(m.name, 14f, COL_TEXT, bold = true))
        texts.addView(label(m.description, 11f, COL_DIM))
        val hint = try { m.statusHint() } catch (_: Throwable) { null }
        if (hint != null) texts.addView(label(hint, 10f, COL_WARN))
        row.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))

        row.addView(Switch(actContext()).apply {
            isChecked = m.isEnabled
            setOnCheckedChangeListener { _, want ->
                ModuleRegistry.setModuleEnabled(m, want)
                log("kotlin ${m.name} → $want")
                rebuildPanel()
            }
        })
        wrap.addView(row)

        if (m.isEnabled && m.settings.isNotEmpty()) {
            val settingsCol = LinearLayout(actContext()).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(6), dp(2), 0, dp(2))
            }
            for (s in m.settings) settingsCol.addView(settingRow(s))
            wrap.addView(settingsCol)
        }
        return wrap
    }

    private fun settingRow(s: KSetting): View {
        val row = LinearLayout(actContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(3), 0, dp(3))
        }
        val nameTv = label(s.label, 12f, COL_TEXT).apply {
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            if (Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) tooltipText = s.desc
        }
        row.addView(nameTv)

        when (s) {
            is BoolSetting -> row.addView(Switch(actContext()).apply {
                isChecked = s.value
                setOnCheckedChangeListener { _, c ->
                    s.value = c
                    ModuleRegistry.savePrefs()
                }
            })

            is FloatSetting -> {
                val tv = label(fmt(s.value), 11f, COL_ACCENT)
                row.addView(tv)
                row.addView(SeekBar(actContext()).apply {
                    max = 1000
                    progress = (((s.value - s.min) / (s.max - s.min)) * 1000f).toInt().coerceIn(0, 1000)
                    layoutParams = LinearLayout.LayoutParams(dp(110), ViewGroup.LayoutParams.WRAP_CONTENT)
                        .apply { leftMargin = dp(6) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            s.value = s.min + (p / 1000f) * (s.max - s.min)
                            tv.text = fmt(s.value)
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) { ModuleRegistry.savePrefs() }
                    })
                })
            }

            is IntSetting -> {
                val tv = label(s.value.toString(), 11f, COL_ACCENT)
                row.addView(tv)
                row.addView(SeekBar(actContext()).apply {
                    max = (s.max - s.min).coerceAtLeast(1)
                    progress = (s.value - s.min).coerceIn(0, max)
                    layoutParams = LinearLayout.LayoutParams(dp(110), ViewGroup.LayoutParams.WRAP_CONTENT)
                        .apply { leftMargin = dp(6) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            s.value = s.min + p
                            tv.text = s.value.toString()
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) { ModuleRegistry.savePrefs() }
                    })
                })
            }

            is EnumSetting -> row.addView(label(s.value, 12f, COL_ACCENT).apply {
                setPadding(dp(8), dp(4), dp(8), dp(4))
                background = rounded(COL_CARD_BG, 6f)
                isClickable = true
                setOnClickListener {
                    s.index = (s.index + 1) % s.options.size
                    text = s.value
                    ModuleRegistry.savePrefs()
                }
            })
        }
        return row
    }

    /**
     * Rebuild the list in place — always deferred: this can fire from inside a
     * Switch listener or from the game thread, and tearing the view tree down
     * mid-dispatch is how "the switch sometimes doesn't react" happens.
     */
    private fun rebuildPanel() {
        if (!panelOpen) return
        main.post {
            if (!panelOpen) return@post
            removePanelNow()
            openPanel()
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // Helpers
    // ══════════════════════════════════════════════════════════════════════

    private fun actContext(): Context =
        activity ?: throw IllegalStateException("overlay not attached")

    private fun dp(v: Int): Int =
        (v * android.content.res.Resources.getSystem().displayMetrics.density).toInt()

    private fun screenW() = actContext().resources.displayMetrics.widthPixels
    private fun screenH() = actContext().resources.displayMetrics.heightPixels

    private fun rounded(color: Int, radiusDp: Float): GradientDrawable = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radiusDp * android.content.res.Resources.getSystem().displayMetrics.density
    }

    private fun label(text: String, sp: Float, color: Int, bold: Boolean = false) =
        TextView(actContext()).apply {
            this.text = text
            textSize = sp
            setTextColor(color)
            if (bold) setTypeface(typeface, Typeface.BOLD)
        }

    private fun toast(msg: String) {
        try {
            Toast.makeText(actContext(), msg, Toast.LENGTH_LONG).show()
        } catch (_: Throwable) {
        }
    }

    private fun uiPrefs() = activity?.applicationContext?.getSharedPreferences("eclient_ui", 0)

    private fun fmt(v: Float): String = if (kotlin.math.abs(v) >= 10f) "%.1f".format(v) else "%.2f".format(v)

    private fun touchName(ev: MotionEvent): String = when (ev.actionMasked) {
        MotionEvent.ACTION_DOWN -> "down"
        MotionEvent.ACTION_UP -> "up"
        MotionEvent.ACTION_MOVE -> "move"
        MotionEvent.ACTION_CANCEL -> "cancel"
        else -> "ev${ev.actionMasked}"
    }

    // ══════════════════════════════════════════════════════════════════════
    // Logging — logcat AND a file the user can actually retrieve
    // ══════════════════════════════════════════════════════════════════════

    private fun openLog(act: Activity) {
        try {
            logName = "${LOG_BASE}_${System.currentTimeMillis() / 1000}.txt"
        } catch (_: Throwable) {
            logName = "$LOG_BASE.txt"
        }
        synchronized(logLock) {
            if (logOut != null) return
            // 1) MediaStore Downloads → shows up in the phone's Files app.
            try {
                if (Build.VERSION.SDK_INT >= 29) {
                    val values = ContentValues().apply {
                        put(MediaStore.MediaColumns.DISPLAY_NAME, logName)
                        put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                        put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/EClient")
                    }
                    val uri = act.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                    if (uri != null) logOut = act.contentResolver.openOutputStream(uri)
                }
            } catch (_: Throwable) {
            }
            // 2) app-private external dir as the always-writable mirror.
            if (logOut == null) {
                try {
                    val dir = act.getExternalFilesDir(null) ?: act.filesDir
                    logOut = java.io.FileOutputStream(java.io.File(dir, logName), true)
                } catch (_: Throwable) {
                }
            }
        }
    }

    private fun closeLog() {
        synchronized(logLock) {
            try { logOut?.flush(); logOut?.close() } catch (_: Throwable) {}
            logOut = null
        }
    }

    private fun log(line: String) {
        val stamped = "${System.currentTimeMillis()} [$TAG] $line"
        try {
            NativeBridge.logLine(stamped)
        } catch (_: Throwable) {
        }
        synchronized(logLock) {
            val out = logOut ?: return
            try {
                out.write((stamped + "\n").toByteArray())
                out.flush()
            } catch (_: Throwable) {
                logOut = null
            }
        }
    }
}

/**
 * A ScrollView that reports every touch that reaches the panel, so the on-screen
 * diagnostic counts real deliveries instead of assuming them. It never consumes
 * anything itself — children and scrolling behave exactly as before.
 */
private class TouchScroll(
    context: Context,
    private val onAnyTouch: (MotionEvent) -> Unit
) : ScrollView(context) {
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        onAnyTouch(ev)
        return super.dispatchTouchEvent(ev)
    }
}
