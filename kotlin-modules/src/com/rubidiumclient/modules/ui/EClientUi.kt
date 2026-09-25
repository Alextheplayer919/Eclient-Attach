package com.rubidiumclient.modules.ui

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
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

/**
 * ═══ E-Client overlay — r25, rebuilt from scratch, dead simple ═══
 *
 * WHY THIS FILE WAS REWRITTEN
 *
 * The r24 overlay could kill game input the moment the menu opened. The cause
 * was structural, not a typo:
 *
 *   * the menu window was MATCH_PARENT with NO FLAG_NOT_TOUCH_MODAL — i.e. a
 *     full-screen, window-modal surface that swallowed EVERY touch on the
 *     device for as long as it existed, and
 *   * `attach()` early-returned when the same activity resumed, so if the
 *     framework tore the sub-windows down (pause / background / rotation) the
 *     overlay's Java references survived while the real windows did not — the
 *     overlay could neither render nor be removed, and the game stayed deaf.
 *
 * The r25 rules that make that class of bug impossible:
 *
 *   1. NOTHING here is ever full-screen and nothing is ever modal. The chip is
 *      ~96dp; the panel is a fixed ~320dp card. Every window carries
 *      FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL, so a touch that does not land
 *      ON a widget belongs to Minecraft. A leaked panel can only ever eat its
 *      own rectangle — never the screen.
 *   2. `attach()` ALWAYS rebuilds (no early return). Called on every
 *      onActivityResumed after the framework may have destroyed our windows.
 *   3. A 1 Hz watchdog compares the Java references against the real window
 *      state: a panel that is not attached gets closed, a missing chip gets
 *      re-added. The two historical bugs ("chip vanishes", "menu eats input")
 *      are both observable and self-healing instead of silent.
 *   4. Close paths are explicit and synchronous: the ✕ button, the chip, the
 *      panic long-press. There is deliberately no tap-outside-to-close, because
 *      that is what forced the old design to put a full-screen scrim on top of
 *      the game (rule 1 beats the convenience).
 *   5. The decor-view OnTouchListener is NOT installed any more (see
 *      tools/minecraft-host-loader.smali): nothing of ours sits in the game's
 *      own input path.
 *
 * Diagnostics stay on-screen on purpose ("touch #N last=(x,y)"): if a tap ever
 * fails to register again, it must be visible on the device, not guessable.
 *
 * ABI: `attach(Activity)` is invoked by name from the loader smali and must
 * keep this exact name and signature. Everything else here is private.
 */
@SuppressLint("ClickableViewAccessibility")
object EClientUi {

    private const val TAG = "EClientUi"

    // ── palette ───────────────────────────────────────────────────────────
    private const val COL_CHIP_BG = 0xE6141A22.toInt()
    private const val COL_PANEL_BG = 0xF210141A.toInt()
    private const val COL_CARD_BG = 0xFF1A222C.toInt()
    private const val COL_TEXT = 0xFFD7DEE7.toInt()
    private const val COL_DIM = 0xFF8A96A3.toInt()
    private const val COL_ACCENT = 0xFF4C9AFF.toInt()
    private const val COL_GOOD = 0xFF3FB950.toInt()
    private const val COL_WARN = 0xFFE3B341.toInt()

    /** Modules the UI hides unless dev mode is on (long-press the title). */
    private val DEV_ONLY = setOf("ECSDump")

    private val main = Handler(Looper.getMainLooper())

    @Volatile private var wm: WindowManager? = null
    @Volatile private var activity: Activity? = null

    @Volatile private var chip: TextView? = null
    @Volatile private var chipLp: WindowManager.LayoutParams? = null

    @Volatile private var panel: View? = null
    @Volatile private var panelOpen = false
    @Volatile private var devMode = false

    // ── on-screen diagnostics (the input bug must stay visible) ───────────
    @Volatile private var panelTouches = 0
    @Volatile private var chipTouches = 0
    @Volatile private var lastTouch = "-"
    @Volatile private var statusLine: TextView? = null

    // ══════════════════════════════════════════════════════════════════════
    // Lifecycle
    // ══════════════════════════════════════════════════════════════════════

    /**
     * Called from EClientLoaderProvider.onActivityResumed (smali, by name).
     * Always rebuilds — never early-returns. See the header for why.
     */
    @JvmStatic
    fun attach(act: Activity) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            attachOnMain(act)
        } else {
            main.post { attachOnMain(act) }
        }
    }

    private fun attachOnMain(act: Activity) {
        try {
            ModuleRegistry.init()
            ModuleRegistry.bind(act)

            val w = act.windowManager ?: run {
                log("attach: no WindowManager")
                return
            }

            removeAllWindows()
            activity = act
            wm = w
            panelOpen = false

            addChip()
            startWatchdog()

            // Module state can change from anywhere (a switch, the native mirror,
            // a module disabling itself like ECSDump does) — the open panel
            // re-reads the truth instead of showing a stale switch.
            ModuleRegistry.onModulesChanged = { rebuildPanel() }

            log("attach ${act.javaClass.simpleName}: chip=${chip != null} panel=closed " +
                "input=pass-through")
        } catch (t: Throwable) {
            log("attach failed: $t")
        }
    }

    /** Detach everything (activity paused/destroyed, or panic). Safe to repeat. */
    @JvmStatic
    fun detach() {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            detachOnMain()
        } else {
            main.post { detachOnMain() }
        }
    }

    private fun detachOnMain() {
        main.removeCallbacks(watchdog)
        removeAllWindows()
        activity = null
        log("detached")
    }

    private fun removeAllWindows() {
        closePanel()
        removeChip()
    }

    // ══════════════════════════════════════════════════════════════════════
    // Window helpers
    // ══════════════════════════════════════════════════════════════════════

    private fun ctx(): Context =
        activity ?: throw IllegalStateException("overlay not attached")

    private fun dp(v: Int): Int =
        (v * android.content.res.Resources.getSystem().displayMetrics.density).toInt()

    private fun screenW() = ctx().resources.displayMetrics.widthPixels
    private fun screenH() = ctx().resources.displayMetrics.heightPixels

    private fun rounded(color: Int, radiusDp: Float): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = radiusDp *
                android.content.res.Resources.getSystem().displayMetrics.density
        }

    private fun label(text: String, sp: Float, color: Int, bold: Boolean = false) =
        TextView(ctx()).apply {
            this.text = text
            textSize = sp
            setTextColor(color)
            if (bold) setTypeface(typeface, Typeface.BOLD)
        }

    private fun toast(msg: String, long: Boolean = false) {
        try {
            Toast.makeText(ctx(), msg, if (long) Toast.LENGTH_LONG else Toast.LENGTH_SHORT).show()
        } catch (_: Throwable) {
        }
    }

    private fun log(line: String) {
        NativeBridge.logLine("$TAG: $line")
    }

    /**
     * The one WindowManager.LayoutParams recipe used by every widget window.
     * NOT_FOCUSABLE  → keys/back stay with the game, we never take window focus.
     * NOT_TOUCH_MODAL→ touches outside this widget are dispatched to the game.
     */
    private fun widgetLp(x: Int, y: Int): WindowManager.LayoutParams {
        val a = activity ?: throw IllegalStateException("overlay not attached")
        return WindowManager.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply {
            token = a.window.attributes.token
            gravity = Gravity.TOP or Gravity.START
            this.x = x
            this.y = y
        }
    }

    private fun uiPrefs() =
        activity?.applicationContext?.getSharedPreferences("eclient_ui", 0)

    // ══════════════════════════════════════════════════════════════════════
    // Window 1 — the chip (always present while attached)
    // ══════════════════════════════════════════════════════════════════════

    private fun addChip() {
        if (chip != null) return
        try {
            val p = uiPrefs()
            val lp = widgetLp(
                p?.getInt("chip_x", dp(12)) ?: dp(12),
                p?.getInt("chip_y", dp(48)) ?: dp(48)
            )
            val v = TextView(ctx()).apply {
                text = "E-Client"
                textSize = 12f
                setTextColor(Color.WHITE)
                setTypeface(typeface, Typeface.BOLD)
                gravity = Gravity.CENTER
                background = rounded(COL_CHIP_BG, 12f)
                alpha = 0.92f
                layoutParams = ViewGroup.LayoutParams(dp(84), dp(34))
            }

            var downX = 0
            var downY = 0
            var downRawX = 0f
            var downRawY = 0f
            var moved = false
            var downAt = 0L

            v.setOnTouchListener { _, ev ->
                when (ev.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downX = lp.x
                        downY = lp.y
                        downRawX = ev.rawX
                        downRawY = ev.rawY
                        moved = false
                        downAt = System.currentTimeMillis()
                        chipTouches++
                        refreshStatus()
                        v.alpha = 1f
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val dx = (ev.rawX - downRawX).toInt()
                        val dy = (ev.rawY - downRawY).toInt()
                        if (kotlin.math.abs(dx) + kotlin.math.abs(dy) > dp(6)) moved = true
                        if (moved) {
                            lp.x = (downX + dx).coerceIn(0, (screenW() - dp(84)).coerceAtLeast(0))
                            lp.y = (downY + dy).coerceIn(0, (screenH() - dp(34)).coerceAtLeast(0))
                            try {
                                wm?.updateViewLayout(v, lp)
                            } catch (t: Throwable) {
                                log("chip move: $t")
                            }
                        }
                        true
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        v.alpha = 0.92f
                        if (moved) {
                            uiPrefs()?.edit()?.putInt("chip_x", lp.x)?.putInt("chip_y", lp.y)?.apply()
                        } else if (ev.actionMasked == MotionEvent.ACTION_UP) {
                            if (System.currentTimeMillis() - downAt > 700L) {
                                panic() // long press = get me out of anything
                            } else {
                                if (panelOpen) closePanel() else openPanel()
                            }
                        }
                        true
                    }
                    else -> false
                }
            }

            wm?.addView(v, lp)
            chip = v
            chipLp = lp
        } catch (t: Throwable) {
            log("addChip failed: $t")
            chip = null
            chipLp = null
        }
    }

    private fun removeChip() {
        val c = chip ?: return
        try {
            if (c.parent != null) wm?.removeViewImmediate(c)
        } catch (t: Throwable) {
            log("removeChip: $t")
        }
        chip = null
        chipLp = null
    }

    // ══════════════════════════════════════════════════════════════════════
    // Window 2 — the panel (exists ONLY while open, and never full-screen)
    // ══════════════════════════════════════════════════════════════════════

    private fun togglePanel() {
        if (panelOpen) closePanel() else openPanel()
    }

    private fun openPanel() {
        if (panel != null) return
        try {
            val body = buildPanel()
            val w = dp(320).coerceAtMost((screenW() * 0.86f).toInt())
            val h = (screenH() * 0.62f).toInt().coerceAtMost(dp(560))
            val lp = WindowManager.LayoutParams(
                w,
                h,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
                PixelFormat.TRANSLUCENT
            ).apply {
                token = activity?.window?.attributes?.token
                gravity = Gravity.TOP or Gravity.START
                x = dp(12)
                y = dp(12)
            }
            wm?.addView(body, lp)
            panel = body
            panelOpen = true
            log("panel opened ${w}x$h px (not modal: outside touches stay with the game)")
        } catch (t: Throwable) {
            panel = null
            panelOpen = false
            log("openPanel failed: $t")
            toast("menu failed to open — see logcat")
        }
    }

    private fun closePanel() {
        val p = panel
        panel = null
        panelOpen = false
        statusLine = null
        if (p == null) return
        try {
            if (p.parent != null) wm?.removeViewImmediate(p)
            log("panel closed")
        } catch (t: Throwable) {
            log("closePanel: $t")
        }
    }

    /** Long-press the chip: close everything, reset positions, never leave input stuck. */
    private fun panic() {
        try {
            closePanel()
            uiPrefs()?.edit()
                ?.putInt("chip_x", dp(12))?.putInt("chip_y", dp(48))
                ?.apply()
            chipLp?.let { lp ->
                lp.x = dp(12)
                lp.y = dp(48)
                chip?.let { c -> wm?.updateViewLayout(c, lp) }
            }
            toast("overlay reset")
            log("panic: overlay reset (panel closed, chip to default)")
        } catch (t: Throwable) {
            log("panic: $t")
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // Watchdog — make the two historical bugs impossible to reach silently
    // ══════════════════════════════════════════════════════════════════════

    private val watchdog = object : Runnable {
        override fun run() {
            try {
                checkWindows()
            } catch (_: Throwable) {
            }
            main.postDelayed(this, 1000L)
        }
    }

    private fun startWatchdog() {
        main.removeCallbacks(watchdog)
        main.postDelayed(watchdog, 1000L)
    }

    private fun checkWindows() {
        val c = chip
        if (c != null && !c.isAttachedToWindow) {
            log("watchdog: chip fell off the window (framework teardown) → re-adding")
            chip = null
            chipLp = null
            addChip()
        } else if (c == null && activity != null) {
            addChip()
        }

        val p = panel
        if (p != null && !p.isAttachedToWindow) {
            log("watchdog: panel fell off the window → dropping the reference")
            panel = null
            panelOpen = false
            statusLine = null
        }
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
        return "touch #$panelTouches chip #$chipTouches last=$lastTouch\n$patch"
    }

    private fun buildPanel(): View {
        val root = DiagScroll(ctx()) { ev ->
            panelTouches++
            lastTouch = "${touchName(ev)} ${ev.x.toInt()},${ev.y.toInt()}"
            refreshStatus()
        }
        root.background = rounded(COL_PANEL_BG, 14f)
        root.setPadding(dp(12), dp(10), dp(12), dp(10))
        root.clipToPadding = false
        root.isClickable = true

        val col = LinearLayout(ctx()).apply { orientation = LinearLayout.VERTICAL }

        // ── header ────────────────────────────────────────────────────────
        val head = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val title = label("E-Client", 16f, Color.WHITE, bold = true).apply {
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        title.setOnLongClickListener {
            devMode = !devMode
            toast(if (devMode) "dev modules shown" else "dev modules hidden")
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

        col.addView(label("tap ✕ or the chip to close · tap outside = game", 10f, COL_DIM))

        statusLine = label(buildStatusText(), 10f, COL_ACCENT)
        col.addView(statusLine)

        // ── native (C++) modules ──────────────────────────────────────────
        val natives = nativeModules()
        if (natives.isNotEmpty()) {
            col.addView(sectionLabel("NATIVE"))
            for (m in natives) col.addView(nativeRow(m))
        }

        // ── Kotlin modules ────────────────────────────────────────────────
        val kotlin = ModuleRegistry.all()
        if (kotlin.isNotEmpty()) {
            col.addView(sectionLabel("MODULES"))
            for (m in kotlin) col.addView(kotlinRow(m))
        }

        root.addView(col)
        return root
    }

    private fun sectionLabel(text: String) =
        label(text, 11f, COL_ACCENT, bold = true).apply { setPadding(0, dp(12), 0, dp(2)) }

    /**
     * Rebuild the list in place. Always deferred to the next main-loop pass:
     * this can be triggered from inside a Switch listener or from the native
     * tick thread (a module disabling itself), and tearing the view tree down
     * mid-dispatch is how "the switch sometimes doesn't react" bugs happen.
     */
    private fun rebuildPanel() {
        if (!panelOpen) return
        main.post {
            if (!panelOpen) return@post
            closePanel()
            openPanel()
        }
    }

    // ── native module rows ────────────────────────────────────────────────

    private data class NativeModule(
        val name: String,
        val category: String,
        val enabled: Boolean,
        val memoryBacked: Boolean,
        val desc: String
    )

    private fun nativeModules(): List<NativeModule> {
        val raw = try {
            NativeBridge.nativeListModules()
        } catch (t: Throwable) {
            log("nativeListModules failed: $t")
            return emptyList()
        }
        val out = ArrayList<NativeModule>()
        for (line in raw.split('\n')) {
            if (line.isBlank()) continue
            val f = line.split('\t')
            if (f.size < 5) continue
            val name = f[0]
            if (!devMode && name in DEV_ONLY) continue
            out.add(
                NativeModule(
                    name = name,
                    category = f[1],
                    enabled = f[2] == "1",
                    memoryBacked = f[3] == "1",
                    desc = f[4]
                )
            )
        }
        return out
    }

    private fun nativeRow(m: NativeModule): View {
        val wrap = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(7), 0, dp(7))
        }
        val row = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val texts = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        texts.addView(label(m.name, 14f, COL_TEXT, bold = true))
        texts.addView(label(m.desc, 11f, COL_DIM))
        if (m.memoryBacked) texts.addView(label("memory patch", 10f, COL_DIM))
        row.addView(texts)

        val sw = Switch(ctx()).apply {
            isChecked = m.enabled
            minWidth = dp(48)
        }
        sw.setOnCheckedChangeListener { _, checked ->
            val ok = try {
                NativeBridge.nativeSetModuleEnabled(m.name, checked)
            } catch (t: Throwable) {
                log("toggle ${m.name}: $t")
                false
            }
            if (!ok) {
                log("toggle rejected: ${m.name} (patch profile not validated?)")
                sw.isChecked = m.enabled // keep showing the truth
                toast("${m.name}: refused — patch profile not validated")
            } else {
                rebuildPanel()
            }
        }
        row.addView(sw)
        wrap.addView(row)
        return wrap
    }

    // ── Kotlin module rows ────────────────────────────────────────────────

    private fun kotlinRow(m: KotlinModule): View {
        val wrap = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(7), 0, dp(7))
        }
        val row = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val texts = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        texts.addView(label(m.name, 14f, COL_TEXT, bold = true))
        texts.addView(label(m.description, 11f, COL_DIM))
        m.statusHint()?.let { texts.addView(label(it, 10f, COL_WARN)) }
        row.addView(texts)

        val sw = Switch(ctx()).apply {
            isChecked = m.isEnabled
            minWidth = dp(48)
        }
        sw.setOnCheckedChangeListener { _, checked ->
            try {
                ModuleRegistry.setModuleEnabled(m, checked)
            } catch (t: Throwable) {
                log("toggle ${m.name}: $t")
            }
            rebuildPanel()
        }
        row.addView(sw)
        wrap.addView(row)

        if (m.isEnabled && m.settings.isNotEmpty()) {
            val settingsCol = LinearLayout(ctx()).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(6), dp(2), 0, dp(2))
            }
            for (s in m.settings) settingsCol.addView(settingRow(s))
            wrap.addView(settingsCol)
        }
        return wrap
    }

    private fun settingRow(s: KSetting): View {
        val row = LinearLayout(ctx()).apply {
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
            is BoolSetting -> row.addView(Switch(ctx()).apply {
                isChecked = s.value
                setOnCheckedChangeListener { _, c ->
                    s.value = c
                    ModuleRegistry.savePrefs()
                }
            })

            is FloatSetting -> {
                val tv = label(fmt(s.value), 11f, COL_ACCENT)
                row.addView(tv)
                row.addView(SeekBar(ctx()).apply {
                    max = 1000
                    progress = (((s.value - s.min) / (s.max - s.min)) * 1000f).toInt()
                        .coerceIn(0, 1000)
                    layoutParams = LinearLayout.LayoutParams(dp(110), ViewGroup.LayoutParams.WRAP_CONTENT)
                        .apply { leftMargin = dp(6) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            s.value = s.min + (p / 1000f) * (s.max - s.min)
                            tv.text = fmt(s.value)
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) {
                            ModuleRegistry.savePrefs()
                        }
                    })
                })
            }

            is IntSetting -> {
                val tv = label(s.value.toString(), 11f, COL_ACCENT)
                row.addView(tv)
                row.addView(SeekBar(ctx()).apply {
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
                        override fun onStopTrackingTouch(sb: SeekBar?) {
                            ModuleRegistry.savePrefs()
                        }
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

    private fun fmt(v: Float): String =
        if (kotlin.math.abs(v) >= 10f) "%.1f".format(v) else "%.2f".format(v)

    private fun touchName(ev: MotionEvent): String = when (ev.actionMasked) {
        MotionEvent.ACTION_DOWN -> "down"
        MotionEvent.ACTION_UP -> "up"
        MotionEvent.ACTION_MOVE -> "move"
        MotionEvent.ACTION_CANCEL -> "cancel"
        else -> "ev${ev.actionMasked}"
    }
}

/**
 * A ScrollView that reports every touch that reaches the panel, so the on-screen
 * diagnostic counts real deliveries instead of assuming them. It never consumes
 * anything itself — children and scrolling behave exactly as before.
 */
private class DiagScroll(
    context: Context,
    private val onAnyTouch: (MotionEvent) -> Unit
) : ScrollView(context) {
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        onAnyTouch(ev)
        return super.dispatchTouchEvent(ev)
    }
}
