package com.rubidiumclient.modules.ui

import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import java.lang.ref.WeakReference

/**
 * Small, allocation-bounded ClickGUI foundation.
 *
 * The UI deliberately has no module discovery, settings, render loop, native
 * input hook, background work, or activity reference. It owns at most one
 * small view tree, which is removed on pause and replaced on the next resume.
 */
object EClientUi {
    private const val PANEL_TAG = "eclient.empty.panel"
    private val main = Handler(Looper.getMainLooper())
    private var rootRef = WeakReference<FrameLayout>(null)

    /** ABI: called by name from minecraft-host-loader.smali. */
    @JvmStatic
    fun attach(activity: Activity) = onMain { mount(activity) }

    /** ABI-safe lifecycle cleanup; called when Minecraft's activity pauses. */
    @JvmStatic
    fun detach() = onMain { unmount() }

    private fun onMain(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action() else main.post(action)
    }

    private fun mount(activity: Activity) {
        unmount()
        val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return
        val density = activity.resources.displayMetrics.density
        val root = FrameLayout(activity)
        rootRef = WeakReference(root)

        val chip = label(activity, "E-Client", 14).apply {
            setPadding(dp(density, 16), dp(density, 10), dp(density, 16), dp(density, 10))
            background = rounded(0xE6192029.toInt(), dp(density, 18))
            setOnClickListener { togglePanel(activity, density) }
        }
        root.addView(chip, FrameLayout.LayoutParams(wrap(), wrap()))
        content.addView(root, FrameLayout.LayoutParams(wrap(), wrap(), Gravity.TOP or Gravity.END).apply {
            topMargin = dp(density, 24)
            marginEnd = dp(density, 16)
        })
    }

    private fun togglePanel(activity: Activity, density: Float) {
        val root = rootRef.get() ?: return
        root.findViewWithTag<View>(PANEL_TAG)?.let {
            root.removeView(it)
            return
        }
        val panel = LinearLayout(activity).apply {
            tag = PANEL_TAG
            orientation = LinearLayout.VERTICAL
            setPadding(dp(density, 20), dp(density, 18), dp(density, 20), dp(density, 18))
            background = rounded(0xF2192029.toInt(), dp(density, 16))
            addView(label(activity, "ClickGUI", 18))
            addView(label(activity, "No modules are registered yet.", 14).apply {
                setTextColor(0xFF9CA9B8.toInt())
                setPadding(0, dp(density, 8), 0, 0)
            })
        }
        root.addView(panel, FrameLayout.LayoutParams(dp(density, 280), wrap()).apply {
            topMargin = dp(density, 48)
        })
    }

    private fun unmount() {
        rootRef.get()?.let { root ->
            (root.parent as? ViewGroup)?.removeView(root)
            root.removeAllViews()
        }
        rootRef = WeakReference(null)
    }

    private fun label(activity: Activity, text: String, size: Int) = TextView(activity).apply {
        this.text = text
        textSize = size.toFloat()
        setTextColor(Color.WHITE)
    }

    private fun rounded(color: Int, radius: Int) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius.toFloat()
    }

    private fun dp(density: Float, value: Int): Int = (value * density).toInt()
    private fun wrap() = ViewGroup.LayoutParams.WRAP_CONTENT
}
