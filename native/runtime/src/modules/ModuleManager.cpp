#include "modules/ModuleManager.h"
#include "profile/PatchManager.h"

namespace eclient_runtime::modules {

ModuleManager& ModuleManager::instance() {
    static ModuleManager mgr;
    return mgr;
}

ModuleManager::ModuleManager() : modules_{
    // Combat
    {"KillAura", "Nearest attackable actor within 4 blocks", "Combat", true, false},
    {"CriticalHit", "Attack-state hook; pending verification", "Combat", true, false},
    {"AutoClickMine", "Skip the automatic mining cooldown routine", "Combat", false, true},

    // Movement
    {"Speed", "Movement hook using the validated 1.21.111 movement routine", "Movement", true, false},
    {"Fly", "Player ability hook using the validated 1.21.111 ability layout", "Movement", true, false},
    {"AlwaysSprint", "Force LocalPlayer sprint state", "Movement", true, false},
    {"Step", "Raise the player auto-step height", "Movement", true, false},
    {"NoSlowDown", "Disable item/block movement slowdown", "Movement", false, true},
    {"Noclip", "Disable solid collision response", "Movement", false, true},
    {"AntiKnockback", "Suppress motion interpolation used by knockback", "Movement", false, true},
    {"NoWaterDrown", "Disable water drowning/gravity hook", "Movement", false, true},
    {"NoLavaDrown", "Disable lava drowning/gravity hook", "Movement", false, true},
    {"SlowDownTriggers", "Disable block movement slowdown trigger", "Movement", false, true},

    // Visual
    {"FullBright", "Force maximum brightness return value", "Visual", false, true},
    {"NoHurtCam", "Disable hurt camera shake", "Visual", false, true},
    {"NoBlur", "Disable fullscreen blur effects", "Visual", false, true},
    {"NoCaveVignette", "Disable the vignette renderer", "Visual", false, true},
    {"NoCamDistortion", "Disable camera portal distortion", "Visual", false, true},
    {"NoBoatRotation", "Disable vehicle camera rotation", "Visual", false, true},
    {"NoCamSleep", "Disable camera sleep fade", "Visual", false, true},
    {"PlaceCamera", "Disable camera blend placement step", "Visual", false, true},
    {"XrayCameraThird", "Disable third-person camera avoidance", "Visual", false, true},
    {"ESP", "Actor enumeration + world-to-screen renderer; pending verification", "Visual", true, false},

    // Player / Misc
    {"NoEmoteCooldown", "Remove the emote cooldown gate", "Player", false, true},
} {}

const std::vector<ModuleState>& ModuleManager::all() const { return modules_; }

bool ModuleManager::setEnabled(const std::string& name, bool enabled) {
    std::lock_guard lock(mutex_);
    for (auto& module : modules_) {
        if (module.name != name) continue;
        if (module.memoryBacked) {
            if (!profile::PatchManager::instance().set(name, enabled)) return false;
        }
        module.enabled = enabled;
        return true;
    }
    return false;
}

bool ModuleManager::enabled(const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (const auto& module : modules_) if (module.name == name) return module.enabled;
    return false;
}

} // namespace eclient_runtime::modules
