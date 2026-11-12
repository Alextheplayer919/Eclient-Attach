#pragma once
// Included after Apollon's game wrappers, variables and ImGui declarations.
#include <atomic>

namespace eclient_fly2_adapter {
// UI thread writes the desired state, game thread alone changes abilities.
static std::atomic<bool> enabled{false};
static LocalPlayer* appliedPlayer = nullptr;
static bool applied = false;

static void draw() {
    bool on = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Fly2 (experimental)", &on)) enabled.store(on, std::memory_order_release);
    if (on) ImGui::TextWrapped("Uses Minecraft flight controls. Flight persists with the menu closed.");
}

static void tick(LocalPlayer* player, bool gameReady) {
    if (!player) { appliedPlayer = nullptr; applied = false; return; }
    if (player != appliedPlayer) { appliedPlayer = player; applied = false; }
    const bool active = enabled.load(std::memory_order_acquire) && gameReady && !panic && !inMenu;
    if (!active && !applied) return;
    Abilities* abilities = player->getAbilities();
    LayeredAbilities* layer = abilities ? abilities->getLayer(2) : nullptr;
    if (!layer) return;
    if (active) {
        // Same engine ability calls as Apollon's own Creative/Force Fly. The
        // game handles its normal joystick, jump/sneak controls and hover.
        layer->setAbility(10, true); // may fly
        layer->setAbility(9, true);  // currently flying
        applied = true;
    } else {
        // Follow Apollon's survival-only cleanup policy. Don't cancel another
        // enabled flight module; never dereference a previous player pointer.
        if (player->isAttackableGamemode()) {
            if (!forceCreativeFly) layer->setAbility(9, false);
            if (!creativeFly) layer->setAbility(10, false);
        }
        applied = false;
    }
}
}
