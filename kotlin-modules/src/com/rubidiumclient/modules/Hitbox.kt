package com.rubidiumclient.modules

/**
 * Hitbox — REGISTERED, honest pending state.
 *
 * In the proxy build this module sends enlarged WIDTH/HEIGHT entity data via
 * SetEntityDataPacket. The attach build has no packet injection, and writing
 * hitbox size straight into each entity's memory requires an offset that is
 * not yet derived from 1.21.111 binaries. Guessing it is how we crashed
 * builds before — so this is intentionally inert until ECSDump-style
 * discovery lands the size field. Nothing is faked.
 */
class Hitbox : KotlinModule(
    name = "Hitbox",
    category = "Combat",
    description = "Bigger enemy hitboxes on YOUR screen",
) {
    private val width = float("Width", "Width", 1.5f, 0.5f, 12f)
    private val height = float("Height", "Height", 1.5f, 0.5f, 12f)
    private val playersOnly = bool("Players-Only", "Players Only", true)
    private val range = float("Range", "Range", 12f, 2f, 32f)

    override fun statusHint() = "Pending: entity size offset not derived yet"

    override fun onTick() {
        // no-op until the per-entity size location is memory-discovered
    }
}
