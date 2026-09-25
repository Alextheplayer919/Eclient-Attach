package com.rubidiumclient.modules.ui

import android.annotation.SuppressLint
import android.app.Activity
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
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
import com.rubidiumclient.modules.BoolSetting
import com.rubidiumclient.modules.EnumSetting
import com.rubidiumclient.modules.FloatSetting
import com.rubidiumclient.modules.IntSetting
import com.rubidiumclient.modules.KSetting
import com.rubidiumclient.modules.KotlinModule
import com.rubidiumclient.modules.ModuleRegistry

/**
 * The in-game overlay, REBUILT (r24) on Android's WindowManager after it
 * became clear that attaching views into Minecraft's own window can never
 * be made touch-safe (the game's input co-exists in the same dispatch tree;
 * any flag mistake = dead game input; any reorder = chip "under the game").
 *
 * The canonical in-process overlay is THREE SEPARATE WINDOWS, each with the
 * exact flags it needs and nothing more:
 *
 *   1. chip window    — tiny, draggable. FLAG_NOT_TOUCH_MODAL ⇒ every touch
 *      that does NOT land on the chip falls through to Minecraft by
 *      construction (WindowManager routes it to the window behind). It
 *      physically cannot block the game.
 *   2. fly window     — same deal for the ▲/▼ column.
 *   3. panel window   — full-screen, added ONLY while the menu is open and
 *      REMOVED the moment it closes. "Forgot to stop absorbing touches" is
 *      structurally impossible: the absorber physically leaves the screen.
 *
 * Windows are TYPE_APPLICATION_PANEL anchored to the activity token — they
 * sit above the game window by type; "slides under the game" is dead too.
 */
@SuppressLint("ClickableViewAccessibility")
object EClientUi {

    private val main = Handler(Looper.getMainLooper())

    @Volatile private var wm: WindowManager? = null
    @Volatile private var activity: Activity? = null

    @Volatile private var chipView: TextView? = null
    @Volatile private var chipLp: WindowManager.LayoutParams? = null

    @Volatile private var flyView: LinearLayout? = null
    @Volatile private var flyLp: WindowManager.LayoutParams? = null

    @Volatile private var panelWin: FrameLayout? = null
    @Volatile private var paneScroll: ScrollView? = null

    // ▲/▼ hold state read by Fly/MotionFly on the game tick thread.
    @Volatile var upHeld: Boolean = false; private set
    @Volatile var downHeld: Boolean = false; private set

    fun flyUpHeld() = upHeld
    fun flyDownHeld() = downHeld

    // ── entry ─────────────────────────────────────────────────────────

    @JvmStatic
    fun attach(act: Activity) {
        val w = act.windowManager ?: return
        ModuleRegistry.init()
        ModuleRegistry.bind(act)

        if (activity === act && chipView != null) return // already attached here
        detachAll() // rotation / new activity instance: old tokens are dead
        activity = act
        wm = w

        addChipWindow()
        addFlyWindow()

        ModuleRegistry.onModulesChanged = {
            main.post {
                try {
                    if (panelWin != null) { // menu open → live-rebuild content
                        val container = panelWin ?: return@post
                        container.removeAllViews()
                        paneScroll = buildPane()
                        container.addView(paneScroll)
                    }
                } catch (t: Throwable) {
                    com.rubidiumclient.modules.NativeBridge.logLine("panel rebuild: $t")
                }
            }
        }
        setFlyButtonsVisible(ModuleRegistry.anyFlyButtonsVisible())
    }

    private fun detachAll() {
        val w = wm ?: return
        fun remove(v: View?) {
            try { if (v != null && v.parent != null) w.removeView(v) }
            catch (_: Throwable) {}
        }
        remove(chipView); chipView = null; chipLp = null
        remove(flyView); flyView = null; flyLp = null
        remove(panelWin); panelWin = null; paneScroll = null
        upHeld = false; downHeld = false
    }

    // ── colors / helpers ──────────────────────────────────────────────

    private fun dp(v: Int): Int =
        (v * android.content.res.Resources.getSystem().displayMetrics.density).toInt()

    private fun rounded(color: Int, radiusDp: Float): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = (radiusDp *
                android.content.res.Resources.getSystem().displayMetrics.density)
        }

    private fun ctx(): android.content.Context =
        activity ?: throw IllegalStateException("not attached")

    private fun label(text: String, sp: Float, color: Int, bold: Boolean = false) =
        TextView(ctx()).apply {
            this.text = text
            textSize = sp
            setTextColor(color)
            if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
        }

    private fun uiPrefs() =
        activity?.applicationContext?.getSharedPreferences("eclient_ui", 0)

    private fun panelLp(): WindowManager.LayoutParams {
        val a = activity!!
        return WindowManager.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            // Modal within its area (whole screen): absorbs everything while
            // open — closed = GONE = physically not there.
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply { token = a.window.attributes.token }
    }

    private fun widgetLp(x: Int, y: Int): WindowManager.LayoutParams {
        val a = activity!!
        return WindowManager.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            // THE fix: only touches landing on this widget are consumed;
            // everything else passes through to the game window behind.
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            token = a.window.attributes.token
            gravity = Gravity.TOP or Gravity.START
            this.x = x
            this.y = y
        }
    }

    private fun screenW() = ctx().resources.displayMetrics.widthPixels
    private fun screenH() = ctx().resources.displayMetrics.heightPixels

    // ── window 1: the chip ────────────────────────────────────────────

    private fun addChipWindow() {
        val p = uiPrefs()
        val lp = widgetLp(
            p?.getInt("chip_x", dp(12)) ?: dp(12),
            p?.getInt("chip_y", dp(48)) ?: dp(48)
        )
        val chip = TextView(ctx()).apply {
            text = "E-Client"
            textSize = 13f
            setTextColor(Color.WHITE)
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            gravity = Gravity.CENTER
            background = rounded(0xCC1E2A38.toInt(), 14f)
            alpha = 0.92f
            isClickable = true
            layoutParams = ViewGroup.LayoutParams(dp(96), dp(40))
        }

        var startX = 0
        var startY = 0
        var downRawX = 0f
        var downRawY = 0f
        var moved = false
        var downAt = 0L
        chip.setOnTouchListener { _, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    startX = lp.x; startY = lp.y
                    downRawX = ev.rawX; downRawY = ev.rawY
                    moved = false
                    downAt = System.currentTimeMillis()
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = (ev.rawX - downRawX).toInt()
                    val dy = (ev.rawY - downRawY).toInt()
                    if (kotlin.math.abs(dx) + kotlin.math.abs(dy) > dp(6)) moved = true
                    lp.x = (startX + dx).coerceIn(0, screenW() - dp(96).coerceAtMost(screenW()))
                    lp.y = (startY + dy).coerceIn(0, screenH() - dp(40).coerceAtMost(screenH()))
                    chip.alpha = 1f
                    try { wm?.updateViewLayout(chip, lp) } catch (_: Throwable) {}
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    chip.alpha = 0.92f
                    if (moved) {
                        uiPrefs()?.edit()?.putInt("chip_x", lp.x)?.putInt("chip_y", lp.y)?.apply()
                        android.widget.Toast.makeText(
                            ctx(), "chip @ ${lp.x},${lp.y}",
                            android.widget.Toast.LENGTH_SHORT).show()
                    } else if (ev.actionMasked == MotionEvent.ACTION_UP) {
                        if (System.currentTimeMillis() - downAt > 700L) {
                            resetEverything() // PANIC recovery: long-press
                        } else {
                            togglePanel()
                        }
                    }
                    true
                }
                else -> false
            }
        }
        wm?.addView(chip, lp)
        chipView = chip
        chipLp = lp
    }

    /** Long-press-chip recovery: close the menu, send widgets to defaults,
     *  release holds, and wipe saved positions. */
    private fun resetEverything() {
        try {
            closePanel()
            upHeld = false; downHeld = false
            uiPrefs()?.edit()
                ?.putInt("chip_x", dp(12))?.putInt("chip_y", dp(48))
                ?.putInt("fly_x", defaultFlyX())?.putInt("fly_y", defaultFlyY())
                ?.apply()
            chipLp?.let { lp ->
                lp.x = dp(12); lp.y = dp(48)
                chipView?.let { v -> wm?.updateViewLayout(v, lp) }
            }
            flyLp?.let { lp ->
                lp.x = defaultFlyX(); lp.y = defaultFlyY()
                flyView?.let { v ->
                    v.visibility = flyViewVisibility()
                    try { wm?.updateViewLayout(v, lp) } catch (_: Throwable) {}
                }
            }
            android.widget.Toast.makeText(
                ctx(), "overlay reset", android.widget.Toast.LENGTH_SHORT).show()
            com.rubidiumclient.modules.NativeBridge.logLine("overlay state reset")
        } catch (t: Throwable) {
            com.rubidiumclient.modules.NativeBridge.logLine("reset: $t")
        }
    }

    // ── window 2: ▲/▼ fly buttons ─────────────────────────────────────

    private fun defaultFlyX() = screenW() - dp(76)
    private fun defaultFlyY() = screenH() - dp(300)

    private fun addFlyWindow() {
        val p = uiPrefs()
        val lp = widgetLp(
            p?.getInt("fly_x", defaultFlyX()) ?: defaultFlyX(),
            p?.getInt("fly_y", defaultFlyY()) ?: defaultFlyY()
        )
        val col = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            visibility = flyViewVisibility()
        }
        col.addView(flyButton("▲", 0xFF2ECC71.toInt()) { upHeld = it })
        col.addView(flyButton("▼", 0xFFE74C3C.toInt()) { downHeld = it })
        wm?.addView(col, lp)
        flyView = col
        flyLp = lp
    }

    private fun flyViewVisibility(): Int =
        if (ModuleRegistry.anyFlyButtonsVisible()) View.VISIBLE else View.GONE

    // Hold-still to fly vs drag-to-reposition on the same views: a touch
    // that MOVES past the drag threshold becomes a window reposition drag
    // (fly hold is cancelled for that gesture); a stationary press flies.
    @Volatile private var flyDragRawX = 0f
    @Volatile private var flyDragRawY = 0f
    @Volatile private var flyDragging = false
    @Volatile private var flyDragStartX = 0
    @Volatile private var flyDragStartY = 0

    private fun flyButton(glyph: String, colorDown: Int,
                          onHeld: (Boolean) -> Unit): View {
        val v = TextView(ctx()).apply {
            text = glyph
            textSize = 22f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(0x99202630.toInt())
                setStroke(dp(2), Color.WHITE)
            }
            alpha = 0.85f
            isClickable = true
        }
        v.layoutParams = LinearLayout.LayoutParams(dp(64), dp(64)).apply {
            topMargin = dp(10)
        }
        v.setOnTouchListener { view, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    flyDragRawX = ev.rawX
                    flyDragRawY = ev.rawY
                    flyDragStartX = flyLp?.x ?: 0
                    flyDragStartY = flyLp?.y ?: 0
                    flyDragging = false
                    onHeld(true)
                    (view.background as GradientDrawable).setColor(colorDown)
                    view.alpha = 1f
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val ddx = ev.rawX - flyDragRawX
                    val ddy = ev.rawY - flyDragRawY
                    if (!flyDragging &&
                        ddx * ddx + ddy * ddy > dp(14) * dp(14)) {
                        flyDragging = true
                        // The hold is cancelled mid-gesture — you're moving
                        // the buttons, not flying.
                        onHeld(false)
                        (view.background as GradientDrawable).setColor(0x99202630.toInt())
                        view.alpha = 0.85f
                    }
                    if (flyDragging) {
                        flyLp?.let { lp ->
                            lp.x = (flyDragStartX + ddx.toInt())
                                .coerceIn(0, screenW() - dp(64))
                            lp.y = (flyDragStartY + ddy.toInt())
                                .coerceIn(0, screenH() - dp(140))
                            flyView?.let { fv ->
                                try { wm?.updateViewLayout(fv, lp) }
                                catch (_: Throwable) {}
                            }
                        }
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (flyDragging) {
                        onHeld(false) // safety: no stale hold after a drag
                        flyLp?.let { lp ->
                            uiPrefs()?.edit()
                                ?.putInt("fly_x", lp.x)?.putInt("fly_y", lp.y)?.apply()
                        }
                        flyDragging = false
                    } else {
                        onHeld(false)
                    }
                    (view.background as GradientDrawable).setColor(0x99202630.toInt())
                    view.alpha = 0.85f
                    true
                }
                else -> false
            }
        }
        return v
    }

    @JvmStatic
    fun setFlyButtonsVisible(visible: Boolean) {
        main.post {
            flyView?.visibility = if (visible) View.VISIBLE else View.GONE
            if (!visible) { upHeld = false; downHeld = false }
        }
    }

    // ── window 3: the module panel (exists ONLY while open) ───────────

    private fun togglePanel() {
        try {
            if (panelWin != null) closePanel() else openPanel()
        } catch (t: Throwable) {
            com.rubidiumclient.modules.NativeBridge.logLine("panel toggle: $t")
        }
    }

    private fun openPanel() {
        if (panelWin != null) return
        val container = FrameLayout(ctx()).apply {
            // Tap anywhere outside the panel card = close.
            isClickable = true
            setOnClickListener { closePanel() }
        }
        paneScroll = buildPane()
        container.addView(paneScroll)
        wm?.addView(container, panelLp())
        panelWin = container
    }

    private fun closePanel() {
        val p = panelWin ?: return
        try { wm?.removeView(p) } catch (_: Throwable) {}
        panelWin = null
        paneScroll = null
    }

    private fun buildPane(): ScrollView {
        val pane = ScrollView(ctx()).apply {
            background = rounded(0xE610141A.toInt(), 16f)
            setPadding(dp(18), dp(14), dp(18), dp(14))
            clipToPadding = false
            // Touches inside the card stay here — outside-close must not fire
            // while the user is actually using the menu.
            isClickable = true
        }
        pane.layoutParams = FrameLayout.LayoutParams(
            dp(340).coerceAtMost((screenW() * 0.88f).toInt()),
            (screenH() * 0.72f).toInt(),
            Gravity.CENTER
        )

        val col = LinearLayout(ctx()).apply { orientation = LinearLayout.VERTICAL }

        // Header: title + explicit ✕ close button.
        val header = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(label("E-Client modules", 17f, Color.WHITE, bold = true).apply {
            layoutParams = LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        })
        header.addView(TextView(ctx()).apply {
            text = "✕"
            textSize = 20f
            setTextColor(0xFFE3B341.toInt())
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            setPadding(dp(10), dp(4), dp(10), dp(4))
            isClickable = true
            setOnClickListener { togglePanel() }
        })
        col.addView(header)
        col.addView(label("Tap outside or ✕ to close · long-press chip = reset overlay",
            11f, 0xFF8E99A5.toInt()))

        val order = listOf("Combat", "Movement", "Visual", "Player", "Misc")
        for (cat in order) {
            val mods = ModuleRegistry.all().filter { it.category == cat }
            if (mods.isEmpty()) continue
            col.addView(label(cat.uppercase(), 12f, 0xFF58A6FF.toInt(), bold = true).apply {
                setPadding(0, dp(14), 0, dp(4))
            })
            for (m in mods) col.addView(moduleRow(m))
        }

        col.addView(label("Native engine stays loaded (tick + memory + patches). " +
            "Kotlin owns the modules & this menu.", 10f, 0xFF626C77.toInt()).apply {
            setPadding(0, dp(16), 0, 0)
        })

        pane.addView(col)
        return pane
    }

    private fun moduleRow(m: KotlinModule): View {
        val wrap = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(8), 0, dp(8))
        }

        val topRow = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val texts = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        texts.addView(label(m.name + " [Kotlin]", 15f, Color.WHITE, bold = true))
        texts.addView(label(m.description, 12f, 0xFF9AA5B1.toInt()))
        m.statusHint()?.let { texts.addView(label(it, 11f, 0xFFE3B341.toInt())) }
        topRow.addView(texts)

        val settingsCol = LinearLayout(ctx()).apply {
            orientation = LinearLayout.VERTICAL
            visibility = if (m.isEnabled && m.settings.isNotEmpty()) View.VISIBLE else View.GONE
            setPadding(dp(6), 0, 0, 0)
        }
        for (s in m.settings) settingsCol.addView(settingRow(s))

        val toggle = Switch(ctx()).apply {
            isChecked = m.isEnabled
            minWidth = dp(52)
        }
        toggle.setOnCheckedChangeListener { _, checked ->
            try {
                ModuleRegistry.setModuleEnabled(m, checked)
                toggle.isChecked = m.isEnabled
                settingsCol.visibility =
                    if (m.isEnabled && m.settings.isNotEmpty()) View.VISIBLE else View.GONE
            } catch (t: Throwable) {
                com.rubidiumclient.modules.NativeBridge.logLine("toggle ${m.name}: $t")
            }
        }
        topRow.addView(toggle)
        wrap.addView(topRow)

        if (m.settings.isNotEmpty()) wrap.addView(settingsCol)
        return wrap
    }

    private fun settingRow(s: KSetting): View {
        val row = LinearLayout(ctx()).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(4), 0, dp(4))
        }
        when (s) {
            is BoolSetting -> {
                row.addView(label(s.label, 13f, 0xFFD0D7DF.toInt()).apply {
                    layoutParams = LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    if (android.os.Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) {
                        tooltipText = s.desc
                    }
                })
                row.addView(Switch(ctx()).apply {
                    isChecked = s.value
                    setOnCheckedChangeListener { _, c ->
                        s.value = c; ModuleRegistry.savePrefs()
                    }
                })
            }
            is FloatSetting -> {
                val valueTv = label(formatFloat(s.value), 12f, 0xFF58A6FF.toInt())
                row.addView(label(s.label, 13f, 0xFFD0D7DF.toInt()).apply {
                    layoutParams = LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    if (android.os.Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) {
                        tooltipText = s.desc
                    }
                })
                row.addView(valueTv)
                row.addView(SeekBar(ctx()).apply {
                    max = 1000
                    progress = (((s.value - s.min) / (s.max - s.min)) * 1000f).toInt()
                    layoutParams = LinearLayout.LayoutParams(dp(120),
                        ViewGroup.LayoutParams.WRAP_CONTENT).apply { leftMargin = dp(8) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            s.value = s.min + (p / 1000f) * (s.max - s.min)
                            valueTv.text = formatFloat(s.value)
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) = ModuleRegistry.savePrefs()
                    })
                })
            }
            is IntSetting -> {
                val valueTv = label(s.value.toString(), 12f, 0xFF58A6FF.toInt())
                row.addView(label(s.label, 13f, 0xFFD0D7DF.toInt()).apply {
                    layoutParams = LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    if (android.os.Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) {
                        tooltipText = s.desc
                    }
                })
                row.addView(valueTv)
                row.addView(SeekBar(ctx()).apply {
                    max = s.max - s.min
                    progress = s.value - s.min
                    layoutParams = LinearLayout.LayoutParams(dp(120),
                        ViewGroup.LayoutParams.WRAP_CONTENT).apply { leftMargin = dp(8) }
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                            if (!fromUser) return
                            s.value = s.min + p
                            valueTv.text = s.value.toString()
                        }
                        override fun onStartTrackingTouch(sb: SeekBar?) {}
                        override fun onStopTrackingTouch(sb: SeekBar?) = ModuleRegistry.savePrefs()
                    })
                })
            }
            is EnumSetting -> {
                row.addView(label(s.label, 13f, 0xFFD0D7DF.toInt()).apply {
                    layoutParams = LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    if (android.os.Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) {
                        tooltipText = s.desc
                    }
                })
                row.addView(TextView(ctx()).apply {
                    text = s.value
                    textSize = 13f
                    setTextColor(0xFF58A6FF.toInt())
                    setPadding(dp(10), dp(6), dp(10), dp(6))
                    background = rounded(0xFF1C2733.toInt(), 8f)
                    isClickable = true
                    setOnClickListener {
                        s.index = (s.index + 1) % s.options.size
                        text = s.value
                        ModuleRegistry.savePrefs()
                    }
                })
            }
        }
        return row
    }

    private fun formatFloat(v: Float): String =
        if (kotlin.math.abs(v) >= 10f) "%.1f".format(v) else "%.2f".format(v)
}
