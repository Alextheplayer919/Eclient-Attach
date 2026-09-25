package com.rubidiumclient.modules

import com.rubidiumclient.modules.ui.EClientUi
import kotlin.math.min
import kotlin.math.sqrt

/**
 * MotionFly ("LeHu-style", ported from Eclient's proxy build).
 *
 * Original is packet-driven (WANT_UP/WANT_DOWN + input vector off
 * PlayerAuthInputPacket, answered with SetEntityMotionPacket). Attach build
 * mappings: WANT_UP/DOWN → the ▲/▼ overlay buttons, packet input vector →
 * the native accel-residue world input dir, motion packet → direct velocity
 * write. All LeHu math kept verbatim; mutually exclusive with Fly.
 */
class MotionFly : KotlinModule(
    name = "MotionFly",
    category = "Movement",
    description = "LeHu-style motion fly (Kotlin) — the fast one",
) {
    private val hBps = float("H-BPS", "H Speed BPS", 46f, 1f, 100f)
    private val upBps = float("Up-BPS", "Up Speed BPS", 19.8f, 1f, 60f)
    private val downBps = float("Down-BPS", "Down Speed BPS", 46f, 1f, 60f)
    private val glide = float("Glide", "Glide", -0.02f, -0.3f, 0f,
        "Idle vertical speed when no button is held (0 = pin-hover)")
    private val upHFactor = float("Up-H-Factor", "Up H Factor", 0.55f, 0.1f, 1f)
    private val downHFactor = float("Down-H-Factor", "Down H Factor", 0.55f, 0.1f, 1f)

    override val usesFlyButtons = true

    private val state3 = FloatArray(3)
    private val dir3 = FloatArray(3)
    private var lastX = 0f
    private var lastY = 0f
    private var lastZ = 0f
    private var haveLast = false

    override fun onEnable() { haveLast = false }

    override fun onTick() {
        if (!NativeBridge.getPosition(state3)) return
        val x = state3[0]; val y = state3[1]; val z = state3[2]
        if (haveLast) {
            val dx = x - lastX; val dy = y - lastY; val dz = z - lastZ
            if (dx * dx + dy * dy + dz * dz > 4.5f * 4.5f) {
                lastX = x; lastY = y; lastZ = z
                return
            }
        }
        lastX = x; lastY = y; lastZ = z
        haveLast = true

        val wantUp = EClientUi.flyUpHeld()
        val wantDown = EClientUi.flyDownHeld()
        val glideV = glide.value

        val moving = NativeBridge.getWorldInputDir(dir3)
        // LeHu early-skip: no input either axis, nothing moving, zero glide.
        if (!wantUp && !wantDown && !moving && glideV == 0f) return

        var horiz = min(hBps.value / 20f, sqrt(5.99).toFloat())
        if (wantUp) horiz *= upHFactor.value
        if (wantDown) horiz *= downHFactor.value

        val vert = when {
            wantUp -> upBps.value / 20f
            wantDown -> -downBps.value / 20f + glideV
            else -> glideV
        }

        val mx = if (moving) dir3[0] * horiz else 0f
        val mz = if (moving) dir3[1] * horiz else 0f
        NativeBridge.setVelocity(mx, vert, mz)
    }
}
