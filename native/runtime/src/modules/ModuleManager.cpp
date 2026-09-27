#include "modules/ModuleManager.h"
#include "kotlin/KotlinBridge.h"
#include "profile/PatchManager.h"

#include <android/log.h>

#include <cmath>

namespace eclient_runtime::modules {
using eclient_runtime::kotlinbridge::setModuleEnabled;

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

// ── Module surface (r25): PvP ONLY ─────────────────────────────────────────
// What used to be here is gone on purpose, code included: KillAura (the crude
// 4-block one), Speed, Fly, MotionFly, Step, AutoClickMine, ESP, Noclip, the
// drown patches, SlowDownTriggers, NoEmoteCooldown and every visual patch.
//
// PatchManager still validates ALL 18 byte sites. That validation is the
// integrity gate for the binary we patch and is deliberately untouched — the
// toggles above are simply no longer exposed, so a hidden patch can never be
// toggled by accident, but a wrong binary still fails closed at boot.
ModuleManager::ModuleManager() : modules_{
    // ── Combat ────────────────────────────────────────────────────────────
    // KillAura2: full Melody (1.20.51) KillAura port. Settings match the
    // Melody module 1:1; the two that need memory we have not discovered yet
    // fail inert instead of degrading the rest.
    {"KillAura2", "Melody KillAura port — rotation math, delay, single/multi, A/B vs KillAura", "Combat", false, false, {
        Setting::real("Range", 3.0f, 1.0f, 12.0f,
            "Melody Range slider (their min was 3.0; 1.0 left available)"),
        Setting::real("WallRange", 0.0f, 0.0f, 12.0f,
            "Melody WallRange. 0 = same as Range. Line-of-sight check is not\n"
            "ported yet, so any value behaves like Range for now."),
        Setting::choice("Mode", {"Single", "Multi"}, 0,
            "Single: attack the closest valid target.\nMulti: attack every valid target in range each attack tick."),
        Setting::choice("Rotation", {"None", "Silent", "Strafe"}, 0,
            "Melody Fruit rotations (yaw/pitch math ported verbatim).\n"
            "Silent: aim the camera rotation at the target — attacks land where\n"
            "we aim, and your view follows.\n"
            "Strafe: turn body and head only, your camera stays where you left\n"
            "it (the closest thing to a silent rotation in-process, where there\n"
            "is no packet to rewrite)."),
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
    {"CriticalHit", "Crit timing hook — registered, pending verification", "Combat", false, false, {}},

    // ── Movement ──────────────────────────────────────────────────────────
    // REWRITTEN (r25). The old version forced SetSprinting every tick, which
    // fought the item-use tick and broke gapples. The rewrite only re-asserts
    // sprint when the player is provably NOT using an item — so it stays inert
    // until the item-use state is derived (fail-closed, never breaks eating).
    {"AlwaysSprint", "Keeps sprint active without fighting item use — PENDING item-use offset, inert until then",
        "Movement", false, false, {}},
    // Fly (r40): flight through the player's abilities. The engine does the
    // flying once the ability is granted, so there is no velocity or position
    // write anywhere in this module — the two approaches that came before it
    // (state-vector velocity, then a position write) are both gone for good.
    // memoryBacked=false ON PURPOSE: Fly flies through the player's abilities
    // (getAbilities/getLayer/setAbility) and writes no bytes at all. It was
    // flagged true by copy-paste from the deleted patch-era Fly, which made
    // every toggle call PatchManager::set("Fly") — a name with no spec — and
    // refuse with "patch profile not validated". See patchCoverageAudit().
    {"Fly", "Engine flight via player abilities — creative-style, works like the game's own fly",
        "Movement", false, false, {
        Setting::choice("Mode", {"Creative", "Force"}, 0,
            "Creative: you are allowed to fly — double-tap jump, vanilla behaviour.\n"
            "Force: airborne immediately."),
        Setting::boolean("Ladder", false,
            "Climb walls while flying, as if they were ladders."),
    }},
    {"AntiKnockback", "Suppress motion interpolation used by knockback", "Movement", false, true, {}},
    {"NoSlowDown", "Disable item/block movement slowdown", "Movement", false, true, {}},

    // ── Dev (hidden in the UI unless dev mode is on) ──────────────────────
    {"ECSDump", "Dev: ECS component deltas for the local player (2 snapshots, ~2s). Used to derive the rotation / hitbox / item-use components.", "Misc", false, false, {}},
} {}

const std::vector<ModuleState>& ModuleManager::all() const { return modules_; }

bool ModuleManager::setEnabled(const std::string& name, bool enabled, std::string* why) {
    std::lock_guard lock(mutex_);
    // NOTE: the Fly ⇄ MotionFly exclusivity pair lived here. Both modules were
    // removed from the product (r25 is PvP-only). If two future modules must
    // never run together, re-add the pair HERE *and* in the Kotlin
    // ModuleRegistry — leaving it in only one of the two is how the old
    // two-registry drift started.
    for (auto& module : modules_) {
        if (module.name != name) continue;
        if (module.memoryBacked) {
            auto& patches = profile::PatchManager::instance();
            // Distinguish the two failures that used to look identical. A module
            // flagged memory-backed with no spec is a WIRING BUG in this table,
            // not a validation problem, and it should say so.
            if (!patches.hasSpec(name)) {
                if (why) {
                    *why = "module is marked memory-backed but has no patch profile named \"" +
                           name + "\"";
                }
                __android_log_print(ANDROID_LOG_ERROR, "EClientModules",
                    "wiring bug: %s is memoryBacked but PatchManager has no spec for it",
                    name.c_str());
                return false;
            }
            if (!patches.set(name, enabled, why)) return false;
        }
        module.enabled = enabled;
        if (module.kotlin) kotlinbridge::setModuleEnabled(name.c_str(), enabled);
        if (why) why->clear();
        return true;
    }
    if (why) *why = "unknown module \"" + name + "\"";
    return false;
}

std::vector<std::string> ModuleManager::patchCoverageAudit() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> problems;
    auto& patches = profile::PatchManager::instance();
    // Nothing to compare against until the profile has been built.
    if (patches.specCount() == 0) return problems;
    for (const auto& m : modules_) {
        if (!m.memoryBacked) continue;
        if (!patches.hasSpec(m.name)) {
            problems.push_back(m.name +
                ": memoryBacked=true but no patch profile with that name exists "
                "(either clear memoryBacked or add/fix the spec name)");
        }
    }
    return problems;
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
