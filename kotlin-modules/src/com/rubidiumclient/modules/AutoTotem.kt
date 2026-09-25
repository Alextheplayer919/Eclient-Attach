package com.rubidiumclient.modules

/**
 * AutoTotem — REGISTERED, honest pending state.
 *
 * Needs two things the attach build cannot do yet: read live inventory
 * contents (to find a totem + check the offhand slot) and perform an
 * inventory move. The proxy build gets both server-side via packets; here
 * they require discovering the player's ContainerManager/Inventory offsets
 * in the 1.21.111 binary. Inert until then — nothing faked.
 */
class AutoTotem : KotlinModule(
    name = "AutoTotem",
    category = "Player",
    description = "Always keeps a totem in the offhand",
) {
    private val retryMs = int("Retry-ms", "Retry interval (ms)", 20, 5, 500)

    override fun statusHint() = "Pending: inventory memory access (no packet layer)"

    override fun onTick() {
        // no-op until inventory offsets land
    }
}
