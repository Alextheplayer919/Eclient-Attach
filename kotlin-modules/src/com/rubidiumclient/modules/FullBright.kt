package com.rubidiumclient.modules

/**
 * FullBright — Kotlin-owned toggle for the existing VALIDATED 1.21.111
 * brightness memory patch (the one heavy thing C++ keeps doing: binary
 * patches). True C++-sparingly: this module is a two-call plumbing layer.
 */
class FullBright : KotlinModule(
    name = "FullBright",
    category = "Visual",
    description = "Force maximum brightness (native patch toggle)",
) {
    override fun onEnable() {
        val ok = NativeBridge.setFullBright(true)
        if (!ok) log("pattern patch failed — binary profile mismatch?")
    }

    override fun onDisable() {
        NativeBridge.setFullBright(false)
    }

    override fun onTick() {
        // The patch applies on toggle; nothing to do per-tick.
    }
}
