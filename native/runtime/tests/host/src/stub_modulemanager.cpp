// stub_modulemanager.cpp — a ModuleManager that does nothing.
//
// Used by the BRIDGE test only: it links AgentState + LoopbackBridge and has no
// interest in module behaviour. The TICK bench links the real ModuleManager.cpp
// instead, on purpose — the per-tick settings lookups are part of what it
// measures, so stubbing them there would hide real cost.
//
// The reasons below are real strings, not blanks: AgentState decides whether a
// failure is "this module only exists on the Kotlin side" by looking at them, so
// an empty reason would silently change that branch.
#include "modules/ModuleManager.h"

namespace eclient_runtime::modules {
ModuleManager& ModuleManager::instance() { static ModuleManager m; return m; }
ModuleManager::ModuleManager() = default;
const std::vector<ModuleState>& ModuleManager::all() const { return modules_; }

bool ModuleManager::setEnabled(const std::string& name, bool, std::string* why) {
    if (why) *why = "unknown module \"" + name + "\"";
    return false;
}
bool ModuleManager::enabled(const std::string&) const { return false; }

bool ModuleManager::setBoolSetting(const std::string&, const std::string&, bool) { return false; }
bool ModuleManager::setFloatSetting(const std::string&, const std::string&, float) { return false; }
bool ModuleManager::setIntSetting(const std::string&, const std::string&, int) { return false; }
bool ModuleManager::setEnumSetting(const std::string&, const std::string&, int) { return false; }

std::vector<std::string> ModuleManager::patchCoverageAudit() const { return {}; }
} // namespace eclient_runtime::modules
