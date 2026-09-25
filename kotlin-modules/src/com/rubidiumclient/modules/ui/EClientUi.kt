package com.rubidiumclient.modules.ui

import android.annotation.SuppressLint
import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
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
 * The in-game overlay written as plain Android Views attached to Minecraft's
 * decor view — replaces the old ImGui/GL menu + GL fly buttons (which cost a
 * GL save/restore every frame; this costs nothing on the game's GPU path).
 *
 *   * draggable "E-Client" chip → tap to open/close the module panel
 *   * panel: category sections, per-module Switch + settings (sliders,
 *     toggles, enum cyclers); persisted via SharedPreferences
 *   * ▲/▼ fly buttons: real Views ⇒ Android's own touch dispatch consumes
 *     their touches (they NEVER spin the camera) and multi-pointer works
 *     natively (one thumb holds ▲ while another steers/looks)
 *
 * Called from the injected loader: EClientLoaderProvider.onActivityResumed.
 */
@SuppressLint("ClickableViewAccessibility")
object EClientUi {

    private const val TAG_ROOT = "eclient_overlay_root"
    private val main = Handler(Looper.getMainLooper())

    @Volatile private var root: FrameLayout? = null
    @Volatile private var panel: ScrollView? = null
    @Volatile private var flyColumn: LinearLayout? = null

    // ▲/▼ hold state read by Fly/MotionFly on the game tick thread.
    @Volatile var upHeld: Boolean = false; private set
    @Volatile var downHeld: Boolean = false; private set

    fun flyUpHeld() = upHeld
    fun flyDownHeld() = downHeld

    // ── entry ─────────────────────────────────────────────────────────

    @JvmStatic
    fun attach(activity: Activity) {
        val decor = activity.window?.decorView as? ViewGroup ?: return
        if (decor.findViewWithTag<ViewGroup>(TAG_ROOT) != null) return

        ModuleRegistry.init()
        ModuleRegistry.bind(activity)

        val overlay = FrameLayout(activity).apply {
            tag = TAG_ROOT
            isClickable = false
            isFocusable = false
        }
        decor.addView(
            overlay,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        root = overlay

        addChip(activity, overlay)
        addFlyButtons(activity, overlay)
        rebuildPanel(overlay)

        ModuleRegistry.onModulesChanged = {
            main.post { root?.let { rebuildPanel(it) } }
        }
        setFlyButtonsVisible(ModuleRegistry.anyFlyButtonsVisible())
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

    private fun label(text: String, sp: Float, color: Int, bold: Boolean = false) =
        TextView(root!!.context).apply {
            this.text = text
            textSize = sp
            setTextColor(color)
            if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
        }

    // ── floating chip ─────────────────────────────────────────────────

    private fun addChip(activity: Activity, overlay: FrameLayout) {
        val chip = TextView(activity).apply {
            text = "E-Client"
            textSize = 13f
            setTextColor(Color.WHITE)
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            gravity = Gravity.CENTER
            background = rounded(0xCC1E2A38.toInt(), 14f)
            elevation = dp(4).toFloat()
        }
        val lp = FrameLayout.LayoutParams(dp(96), dp(40),
            Gravity.END or Gravity.TOP).apply {
            topMargin = dp(120); rightMargin = dp(16)
        }
        chip.layoutParams = lp
        chip.isClickable = true

        var dragDX = 0f
        var dragDY = 0f
        var moved = false
        chip.setOnTouchListener { v, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dragDX = v.x - ev.rawX
                    dragDY = v.y - ev.rawY
                    moved = false
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val nx = ev.rawX + dragDX
                    val ny = ev.rawY + dragDY
                    if (kotlin.math.abs(nx - v.x) + kotlin.math.abs(ny - v.y) > 8f) moved = true
                    val parent = v.parent as View
                    v.x = nx.coerceIn(0f, (parent.width - v.width).toFloat())
                    v.y = ny.coerceIn(0f, (parent.height - v.height).toFloat())
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (!moved) togglePanel()
                    true
                }
                else -> false
            }
        }
        overlay.addView(chip)
    }

    private fun togglePanel() {
        val p = panel ?: return
        val show = p.visibility != View.VISIBLE
        p.visibility = if (show) View.VISIBLE else View.GONE
        if (show) rebuildPanel(p)
    }

    // ── fly buttons ───────────────────────────────────────────────────

    private fun flyButton(activity: Activity, glyph: String, colorDown: Int,
                          onHeld: (Boolean) -> Unit): View {
        val v = TextView(activity).apply {
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
                    onHeld(true)
                    (view.background as GradientDrawable).setColor(colorDown)
                    view.alpha = 1f
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    onHeld(false)
                    (view.background as GradientDrawable).setColor(0x99202630.toInt())
                    view.alpha = 0.85f
                    true
                }
                else -> false
            }
        }
        return v
    }

    private fun addFlyButtons(activity: Activity, overlay: FrameLayout) {
        val col = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            visibility = View.GONE
        }
        col.addView(flyButton(activity, "▲", 0xFF2ECC71.toInt()) { upHeld = it })
        col.addView(flyButton(activity, "▼", 0xFFE74C3C.toInt()) { downHeld = it })
        col.layoutParams = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.END or Gravity.BOTTOM
        ).apply {
            rightMargin = dp(20)
            bottomMargin = dp(210)
        }
        overlay.addView(col)
        flyColumn = col
    }

    @JvmStatic
    fun setFlyButtonsVisible(visible: Boolean) {
        main.post {
            val v = if (visible) View.VISIBLE else View.GONE
            flyColumn?.visibility = v
            if (!visible) { upHeld = false; downHeld = false }
        }
    }

    // ── module panel ──────────────────────────────────────────────────

    private fun rebuildPanel(container: ViewGroup) {
        val activity = container.context
        val pane = ScrollView(activity).apply {
            background = rounded(0xE610141A.toInt(), 16f)
            visibility = panel?.visibility ?: View.GONE
            setPadding(dp(18), dp(14), dp(18), dp(14))
            clipToPadding = false
        }
        val lp = FrameLayout.LayoutParams(
            dp(340).coerceAtMost(
                (container.resources.displayMetrics.widthPixels * 0.88f).toInt()),
            (container.resources.displayMetrics.heightPixels * 0.72f).toInt(),
            Gravity.CENTER
        )
        pane.layoutParams = lp

        // Replace old panel in place.
        val oldIndex = panel?.let { container.indexOfChild(it) } ?: -1
        if (oldIndex >= 0) container.removeView(panel)

        val col = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        col.addView(label("E-Client modules", 17f, Color.WHITE, bold = true))
        col.addView(label("Drag the E-Client chip to move it · tap to open/close",
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
        if (oldIndex >= 0) container.addView(pane, oldIndex, lp) else container.addView(pane, lp)
        panel = pane
    }

    private fun moduleRow(m: KotlinModule): View {
        val activity = root!!.context
        val wrap = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(8), 0, dp(8))
        }

        val topRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val texts = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        val kotlinTag = " [Kotlin]"
        texts.addView(label(m.name + kotlinTag, 15f, Color.WHITE, bold = true))
        texts.addView(label(m.description, 12f, 0xFF9AA5B1.toInt()))
        m.statusHint()?.let { texts.addView(label(it, 11f, 0xFFE3B341.toInt())) }
        topRow.addView(texts)

        val toggle = Switch(activity).apply {
            isChecked = m.isEnabled
            minWidth = dp(52)
        }
        toggle.setOnCheckedChangeListener { _, checked ->
            ModuleRegistry.setModuleEnabled(m, checked)
            if (m.isEnabled != checked) toggle.isChecked = m.isEnabled
        }
        topRow.addView(toggle)
        wrap.addView(topRow)

        val settingsCol = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            visibility = if (m.isEnabled && m.settings.isNotEmpty()) View.VISIBLE else View.GONE
            setPadding(dp(6), 0, 0, 0)
        }
        for (s in m.settings) settingsCol.addView(settingRow(s))
        if (m.settings.isNotEmpty()) {
            wrap.addView(settingsCol)
            toggle.setOnCheckedChangeListener { _, checked ->
                ModuleRegistry.setModuleEnabled(m, checked)
                toggle.isChecked = m.isEnabled
                settingsCol.visibility =
                    if (m.isEnabled) View.VISIBLE else View.GONE
            }
        }
        return wrap
    }

    private fun settingRow(s: KSetting): View {
        val activity = root!!.context
        val row = LinearLayout(activity).apply {
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
                row.addView(Switch(activity).apply {
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
                })
                if (android.os.Build.VERSION.SDK_INT >= 26 && s.desc.isNotEmpty()) {
                    (row.getChildAt(0) as TextView).tooltipText = s.desc
                }
                row.addView(valueTv)
                row.addView(SeekBar(activity).apply {
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
                row.addView(SeekBar(activity).apply {
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
                row.addView(TextView(activity).apply {
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
