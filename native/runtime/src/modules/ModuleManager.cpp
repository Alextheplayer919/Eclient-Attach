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

// The ClickGUI foundation ships with no native modules. Retain the manager API
// so future verified modules have one registration point, but do not expose or
// activate gameplay behavior until an individual module is reviewed.
ModuleManager::ModuleManager() : modules_{} {}

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
            // BUILD THE PROFILE FIRST. hasSpec() answers against the spec table
            // that initialize() fills in, so asking it about an unbuilt profile
            // returns false for EVERY module — and the code below would then
            // accuse this table of a wiring bug that does not exist, while
            // hiding the real reason (the profile was never validated). r43
            // introduced exactly that: it consulted hasSpec() before anything
            // ensured the profile had been built, which also removed the lazy
            // initialize() the old set() call used to perform.
            if (!patches.ready() && !patches.initialize()) {
                if (why) *why = "patch profile not validated (" + patches.status() + ")";
                return false;
            }
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
