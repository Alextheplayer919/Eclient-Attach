#pragma once
// Included after Apollon's game wrappers, variables and ImGui declarations.
#include <atomic>
#include <cstring>
#include "fly2_limits.h"

namespace eclient_fly2_adapter {
// UI thread writes the desired state, game thread alone changes abilities.
static std::atomic<bool> enabled{false};
static std::atomic<float> horizontalBps{46.0f}, upBps{19.8f}, downBps{46.0f};
static LocalPlayer* appliedPlayer = nullptr;
static bool applied = false;

static void draw() {
    bool on = enabled.load(std::memory_order_relaxed);
    if (ImGui::Button(on ? "Disable##fly2" : "Enable##fly2")) {
        on = !on;
        enabled.store(on, std::memory_order_release);
    }
    ImGui::SameLine(); ImGui::TextUnformatted(on ? "Enabled" : "Disabled");
    ImGui::TextWrapped("Minecraft flight controls. Speed limits apply only to existing motion; no server bypass is guaranteed.");
    ImGui::TextWrapped("Speed limiting is unavailable with Anti Knockback enabled.");
    auto slider = [](const char* label, std::atomic<float>& setting, float maximum) {
        float value = setting.load(std::memory_order_relaxed);
        if (ImGui::SliderFloat(label, &value, 1.0f, maximum, "%.1f BPS")) setting.store(value, std::memory_order_relaxed);
    };
    slider("Horizontal limit##fly2", horizontalBps, 100.0f);
    slider("Up limit##fly2", upBps, 60.0f);
    slider("Down limit##fly2", downBps, 60.0f);
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
        // In the pinned ARM64 profile the position pointer is the beginning of
        // the state vector; velocity is +0x18. The validated lerpMotion entry
        // copies the vector there. Never write if Anti Knockback patched it.
#ifndef ECLIENT_FLY2_TEST
        const auto address = getAbsoluteAddress(MineLib, Offsets64::LerpMotion);
        const unsigned char original[] = {0x08, 0x04, 0x41, 0xf9};
        vec3_t* position = player->getPosition();
        if (!antiKnockback && address && position && std::memcmp((const void*)address, original, sizeof(original)) == 0) {
            vec3_t velocity = *reinterpret_cast<const vec3_t*>(reinterpret_cast<const char*>(position) + 0x18);
            const eclient::fly2::Limits limits{horizontalBps.load(), upBps.load(), downBps.load()};
            if (eclient::fly2::capVelocity(velocity.x, velocity.y, velocity.z, limits)) {
                using Lerp = void (*)(LocalPlayer*, const vec3_t*);
                reinterpret_cast<Lerp>(address)(player, &velocity);
            }
        }
#endif
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
