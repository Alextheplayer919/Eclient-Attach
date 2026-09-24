#include "modules/ModuleManager.h"
#include "profile/PatchManager.h"

#include <cmath>

namespace eclient_runtime::modules {

Setting Setting::boolean(std::string key, bool def, std::string tooltip) {
    Setting s;
    s.key = std::move(key);
    s.label = s.key;
    s.tooltip = std::move(tooltip);
    s.type = Type::Bool;
    s.boolValue = def;
    s.boolDefault = def;
    return s;
}

Setting Setting::integer(std::string key, int def, int min, int max, std::string tooltip) {
    Setting s;
    s.key = std::move(key);
    s.label = s.key;
    s.tooltip = std::move(tooltip);
    s.type = Type::Int;
    s.intValue = def;
    s.intDefault = def;
    s.intMin = min;
    s.intMax = max;
    return s;
}

Setting Setting::real(std::string key, float def, float min, float max, std::string tooltip) {
    Setting s;
    s.key = std::move(key);
    s.label = s.key;
    s.tooltip = std::move(tooltip);
    s.type = Type::Float;
    s.floatValue = def;
    s.floatDefault = def;
    s.floatMin = min;
    s.floatMax = max;
    return s;
}

Setting Setting::choice(std::string key, std::vector<std::string> options, int def, std::string tooltip) {
    Setting s;
    s.key = std::move(key);
    s.label = s.key;
    s.tooltip = std::move(tooltip);
    s.type = Type::Enum;
    s.options = std::move(options);
    s.enumValue = def;
    s.enumDefault = def;
    return s;
}

ModuleManager& ModuleManager::instance() {
    static ModuleManager mgr;
    return mgr;
}

ModuleManager::ModuleManager() : modules_{
    // Combat
    {"KillAura", "Nearest attackable actor within 4 blocks", "Combat", true, false, {}},

    // KillAura2: full Melody (1.20.51) KillAura port. Settings match the
    // Melody module 1:1; the two that need memory we have not discovered yet
    // (see tooltip notes) fail inert instead of degrading the rest.
    {"KillAura2", "Melody KillAura port — rotation math, delay, single/multi, A/B vs KillAura", "Combat", true, false, {
        Setting::real("Range", 3.0f, 1.0f, 12.0f,
            "Melody Range slider (their min was 3.0; 1.0 left available)"),
        Setting::real("WallRange", 0.0f, 0.0f, 12.0f,
            "Melody WallRange. 0 = same as Range. Line-of-sight check is not\n"
            "ported yet, so any value behaves like Range for now."),
        Setting::choice("Mode", {"Single", "Multi"}, 0,
            "Single: attack the closest valid target.\nMulti: attack every valid target in range each attack tick."),
        Setting::choice("Rotation", {"None", "Silent", "Strafe"}, 0,
            "Melody Fruit rotations (yaw/pitch math IS ported and used).\n"
            "Silent/Strafe need the rotation component address, which the\n"
            "ECSDump debug module is there to discover — until then they\n"
            "behave like None."),
        Setting::choice("Switch", {"None", "Full", "Silent"}, 0,
            "Melody weapon-switch mode. Needs inventory access (still being\n"
            "derived) — Full/Silent currently act as None."),
        Setting::boolean("Hurttime", false,
            "Melody Hurttime: skip targets inside damage invulnerability.\n"
            "Implemented via real-time health-drop tracking (same effect, no\n"
            "hurtTime field needed)."),
        Setting::integer("Delay", 2, 0, 20,
            "Ticks between attacks (Melody default 2). Lower = faster."),
        Setting::boolean("PlayersOnly", false,
            "Only attack actors whose entity type id matches the local\n"
            "player's own (the same classification melody called isPlayer)."),
    }},
    {"CriticalHit", "Attack-state hook; pending verification", "Combat", true, false, {}},
    {"AutoClickMine", "Skip the automatic mining cooldown routine", "Combat", false, true, {}},

    // Movement
    {"Speed", "Movement hook using the validated 1.21.111 movement routine", "Movement", true, false, {}},
    // Fly: LeHu/MotionFly-style native port — momentum-steered
    // horizontal speed that RESPECTS analog stick magnitude, glide/hover/
    // jetpack vertical modes, anti-rubberband teleport skip. Direct
    // state-vector velocity write (LerpMotion disasm-proven actor+0x208/+0x18).
    {"Fly", "LeHu MotionFly native port (analog-accurate speed, glide/jetpack verticals)", "Movement", true, false, {
        Setting::real("HSpeed BPS", 46.0f, 1.0f, 100.0f,
            "Horizontal speed in blocks/second at FULL stick — LeHu default 46.\n"
            "Steer by walking; stick tilt scales speed."),
        Setting::real("Glide", -0.02f, -0.3f, 0.0f,
            "Fall speed per tick when not climbing — LeHu default -0.02\n"
            "(slow smooth descent); 0 = dead hover."),
        Setting::real("UpSpeed BPS", 19.8f, 1.0f, 60.0f,
            "Climb speed in blocks/second while ascending — LeHu default 19.8.\n"
            "Tap JUMP to climb (each tap sustains ~0.5 s, then you glide)."),
        Setting::real("Up H Factor", 0.55f, 0.1f, 1.0f,
            "Horizontal speed multiplier while climbing — LeHu default 0.55."),
        Setting::choice("VerticalMode", {"Glide", "Hover", "Jetpack"}, 2,
            "Glide: constant slow descent (MotionFly neutral).\n"
            "Hover: no vertical drift at all.\n"
            "Jetpack (default): glide + tap JUMP to climb — closest to the\n"
            "LeHu JUMPING-flag behaviour until hold-to-ascend unlocks\n"
            "(run ECSDump while spamming jump+sneak buttons!)."),
    }},
    {"AlwaysSprint", "Force LocalPlayer sprint state", "Movement", true, false, {}},
    {"Step", "Raise the player auto-step height", "Movement", true, false, {}},
    {"NoSlowDown", "Disable item/block movement slowdown", "Movement", false, true, {}},
    {"Noclip", "Disable solid collision response", "Movement", false, true, {}},
    {"AntiKnockback", "Suppress motion interpolation used by knockback", "Movement", false, true, {}},
    {"NoWaterDrown", "Disable water drowning/gravity hook", "Movement", false, true, {}},
    {"NoLavaDrown", "Disable lava drowning/gravity hook", "Movement", false, true, {}},
    {"SlowDownTriggers", "Disable block movement slowdown trigger", "Movement", false, true, {}},

    // Visual
    {"FullBright", "Force maximum brightness return value", "Visual", false, true, {}},
    {"NoHurtCam", "Disable hurt camera shake", "Visual", false, true, {}},
    {"NoBlur", "Disable fullscreen blur effects", "Visual", false, true, {}},
    {"NoCaveVignette", "Disable the vignette renderer", "Visual", false, true, {}},
    {"NoCamDistortion", "Disable camera portal distortion", "Visual", false, true, {}},
    {"NoBoatRotation", "Disable vehicle camera rotation", "Visual", false, true, {}},
    {"NoCamSleep", "Disable camera sleep fade", "Visual", false, true, {}},
    {"PlaceCamera", "Disable camera blend placement step", "Visual", false, true, {}},
    {"XrayCameraThird", "Disable third-person camera avoidance", "Visual", false, true, {}},
    {"ESP", "Actor enumeration + world-to-screen renderer; pending verification", "Visual", true, false, {}},

    // Player / Misc
    {"NoEmoteCooldown", "Remove the emote cooldown gate", "Player", false, true, {}},

    // Misc / debug
    {"ECSDump", "Debug: dump local player's ECS components to logcat (self-disables)", "Misc", true, false, {}},
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

Setting* ModuleManager::findSetting(const std::string& module, const std::string& key) {
    for (auto& m : modules_) {
        if (m.name != module) continue;
        for (auto& s : m.settings) if (s.key == key) return &s;
        return nullptr;
    }
    return nullptr;
}

const Setting* ModuleManager::findSetting(const std::string& module, const std::string& key) const {
    for (const auto& m : modules_) {
        if (m.name != module) continue;
        for (const auto& s : m.settings) if (s.key == key) return &s;
        return nullptr;
    }
    return nullptr;
}

bool ModuleManager::boolSetting(const std::string& m, const std::string& k, bool fallback) const {
    std::lock_guard lock(mutex_);
    const Setting* s = findSetting(m, k);
    return (s && s->type == Setting::Type::Bool) ? s->boolValue : fallback;
}

int ModuleManager::intSetting(const std::string& m, const std::string& k, int fallback) const {
    std::lock_guard lock(mutex_);
    const Setting* s = findSetting(m, k);
    return (s && s->type == Setting::Type::Int) ? s->intValue : fallback;
}

float ModuleManager::floatSetting(const std::string& m, const std::string& k, float fallback) const {
    std::lock_guard lock(mutex_);
    const Setting* s = findSetting(m, k);
    return (s && s->type == Setting::Type::Float) ? s->floatValue : fallback;
}

int ModuleManager::enumSetting(const std::string& m, const std::string& k, int fallback) const {
    std::lock_guard lock(mutex_);
    const Setting* s = findSetting(m, k);
    return (s && s->type == Setting::Type::Enum) ? s->enumValue : fallback;
}

bool ModuleManager::setBoolSetting(const std::string& m, const std::string& k, bool v) {
    std::lock_guard lock(mutex_);
    Setting* s = findSetting(m, k);
    if (!s || s->type != Setting::Type::Bool) return false;
    s->boolValue = v;
    return true;
}

bool ModuleManager::setIntSetting(const std::string& m, const std::string& k, int v) {
    std::lock_guard lock(mutex_);
    Setting* s = findSetting(m, k);
    if (!s || s->type != Setting::Type::Int || v < s->intMin || v > s->intMax) return false;
    s->intValue = v;
    return true;
}

bool ModuleManager::setFloatSetting(const std::string& m, const std::string& k, float v) {
    std::lock_guard lock(mutex_);
    Setting* s = findSetting(m, k);
    if (!s || s->type != Setting::Type::Float || !std::isfinite(v) ||
        v < s->floatMin || v > s->floatMax) return false;
    s->floatValue = v;
    return true;
}

bool ModuleManager::setEnumSetting(const std::string& m, const std::string& k, int v) {
    std::lock_guard lock(mutex_);
    Setting* s = findSetting(m, k);
    if (!s || s->type != Setting::Type::Enum || v < 0 || v >= static_cast<int>(s->options.size()))
        return false;
    s->enumValue = v;
    return true;
}

} // namespace eclient_runtime::modules
