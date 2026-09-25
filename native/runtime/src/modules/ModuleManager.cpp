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
    // Fly v3 — from scratch, input-observer based: every tick we subtract
    // our own previous velocity write from the current one; the residue is
    // the player's pure input acceleration, so releasing the stick writes 0
    // (dead stop + hover) and stick tilt scales speed exactly.
    {"Fly", "Fly v3 — hover/exact speed from scratch (tap jump to climb, glide to land)", "Movement", true, false, {
        Setting::real("HSpeed BPS", 12.0f, 1.0f, 100.0f,
            "Horizontal speed in blocks/second at FULL stick.\n"
            "Steer with the stick; tilt = proportional speed; release = stop."),
        Setting::real("Glide", -0.02f, -0.3f, 0.0f,
            "Descend-to-land speed per tick in Glide/Jetpack modes.\n"
            "-0.02 = slow drift down; make it more negative to land faster."),
        Setting::real("UpSpeed BPS", 10.0f, 1.0f, 60.0f,
            "Climb speed in blocks/second while a jump impulse is sustained\n"
            "(Jetpack mode: tap JUMP, each tap climbs ~0.5 s)."),
        Setting::choice("VerticalMode", {"Hover", "Glide", "Jetpack"}, 2,
            "Hover: dead pin, no vertical drift.\n"
            "Glide: descend at the Glide slider (use this to land).\n"
            "Jetpack (default): glide down gently, tap JUMP to climb.\n"
            "Hold-jump/hold-sneak verticals unlock after ECS input-flag\n"
            "discovery (run ECSDump while spamming jump+sneak!)."),
        Setting::choice("WriteMode", {"Velocity", "Position"}, 0,
            "Velocity: fly writes the velocity field (default).\n"
            "Position: fly moves the position field directly + zeroes\n"
            "velocity — fallback if the server/game ignores or overwrites\n"
            "velocity writes (check flydebug.txt if unsure)."),
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
    {"ECSDump", "Debug: dump local player's ECS components (2 snapshots, ~2s apart;\nlive value deltas) to logcat + Download/EClient/ecsdump.txt (self-disables)", "Misc", true, false, {}},
    {"FlyDebug", "Debug: log fly internals per tick (240 ticks, self-disables) to logcat\n+ Download/EClient/flydebug.txt", "Misc", true, false, {}},
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
