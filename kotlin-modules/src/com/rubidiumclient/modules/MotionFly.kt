package com.rubidiumclient.modules

import kotlin.math.min
import kotlin.math.sqrt

/**
 * MotionFly ("LeHu-style", ported from Eclient's proxy build).
 *
 * The original is a PROXY module: it reads WANT_UP/WANT_DOWN and the input
 * vector off PlayerAuthInputPacket and answers with SetEntityMotionPacket.
 * The attach build has no packet layer, so:
 *   WANT_UP   → our on-screen ▲ button held   (NativeBridge.buttonUp)
 *   WANT_DOWN → our on-screen ▼ button held   (NativeBridge.buttonDown)
 *   packet input vector → our accel-residue world input dir (the joystick)
 *   SetEntityMotionPacket → direct velocity write (same sink as the C++ Fly)
 *
 * Everything else is the LeHu math verbatim: BPS speeds with the
 * sqrt(5.99)/tick clamp, up/down horizontal factors, glide, and the
 * >4.5-block anti-rubberband skip. Fly ⇄ MotionFly are mutually exclusive in
 * the native ModuleManager, so the two never fight over the velocity field.
 */
class MotionFly : KotlinModule(
    name = "MotionFly",
    category = "Movement",
    description = "LeHu-style fly (Kotlin)",
) {
    // Scratch buffers — reused every tick, zero per-tick allocation.
    private val state3 = FloatArray(3)
    private val dir3 = FloatArray(3)

    private var lastX = 0f
    private var lastY = 0f
    private var lastZ = 0f
    private var haveLast = false

    override fun onEnable() {
        haveLast = false // re-arm the anti-rubberband anchor
    }

    override fun onTick() {
        if (!NativeBridge.getPosition(state3)) return
        val x = state3[0]
        val y = state3[1]
        val z = state3[2]

        // LeHu anti-rubberband: >4.5 blocks in one tick = server teleport/correction.
        if (haveLast) {
            val dx = x - lastX
            val dy = y - lastY
            val dz = z - lastZ
            if (dx * dx + dy * dy + dz * dz > 4.5f * 4.5f) {
                lastX = x; lastY = y; lastZ = z
                return
            }
        }
        lastX = x; lastY = y; lastZ = z
        haveLast = true

        val wantUp = NativeBridge.buttonUp()
        val wantDown = NativeBridge.buttonDown()
        val glide = floatSetting("Glide", -0.02f)

        // LeHu early-skip: nothing asked, nothing moving, glide at zero.
        val moving = NativeBridge.getWorldInputDir(dir3)
        if (!wantUp && !wantDown && !moving && glide == 0f) return

        // Horizontal speed: BPS → per-tick, clamped, modified by vertical state.
        var horiz = min(floatSetting("H-BPS", 46f) / 20f, sqrt(5.99).toFloat())
        if (wantUp) horiz *= floatSetting("Up-H-Factor", 0.55f)
        if (wantDown) horiz *= floatSetting("Down-H-Factor", 0.55f)

        // Vertical speed — LeHu rules.
        val vert = when {
            wantUp -> floatSetting("Up-BPS", 19.8f) / 20f
            wantDown -> -floatSetting("Down-BPS", 46f) / 20f + glide
            else -> glide
        }

        val mx = if (moving) dir3[0] * horiz else 0f
        val mz = if (moving) dir3[1] * horiz else 0f
        NativeBridge.setVelocity(mx, vert, mz)
    }
}
