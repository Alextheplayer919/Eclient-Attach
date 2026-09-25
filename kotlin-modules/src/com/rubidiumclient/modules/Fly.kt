package com.rubidiumclient.modules

/**
 * Fly — E-Client's own fly, Kotlin edition.
 *
 * State machine (identical proven behavior to the native v5 build):
 *   hold ▲  → velocity.y = +V-Speed   (ascend)
 *   hold ▼  → velocity.y = −V-Speed   (descend)
 *   neither → velocity.y =  0.0f      (TRUE hover — exactly zero, so nothing
 *                                      exists for an anticheat to "correct";
 *                                      no glide creep ⇒ no lagback stutter)
 *
 * Horizontal: vanilla input acceleration residue (measured natively via the
 * accel-residue bridge) normalized and projected at H-Speed. The ▲/▼ buttons
 * are real Android Views (ui/EClientUi) — their touches are consumed by
 * Android's own dispatch, so they can never spin the camera, and holding ▲
 * with one thumb while steering/looking with another is native multi-pointer.
 *
 * Anti-rubberband: >4.5 blocks moved in a tick = server teleport/correction,
 * skip the write and re-anchor.
 */
class Fly : KotlinModule(
    name = "Fly",
    category = "Movement",
    description = "Hold ▲ to rise · release to hover in place · ▼ to sink",
) {
    private val hSpeed = float("H-Speed", "H-Speed", 1.0f, 0.2f, 3.0f,
        "Horizontal fly speed (blocks/tick)")
    private val vSpeed = float("V-Speed", "V-Speed", 0.5f, 0.2f, 2.0f,
        "Vertical speed while holding ▲ or ▼ (blocks/tick)")

    override val usesFlyButtons = true

    private val state3 = FloatArray(3)
    private val dir3 = FloatArray(3)

    private var lastX = 0f
    private var lastY = 0f
    private var lastZ = 0f
    private var haveLast = false

    override fun onEnable() {
        haveLast = false
    }

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

        val ui = com.rubidiumclient.modules.ui.EClientUi
        val up = ui.flyUpHeld()
        val down = ui.flyDownHeld()
        val vert = when {
            up && !down -> vSpeed.value
            down && !up -> -vSpeed.value
            else -> 0.0f
        }

        val moving = NativeBridge.getWorldInputDir(dir3)
        val mx = if (moving) dir3[0] * hSpeed.value else 0f
        val mz = if (moving) dir3[1] * hSpeed.value else 0f
        NativeBridge.setVelocity(mx, vert, mz)
    }
}
