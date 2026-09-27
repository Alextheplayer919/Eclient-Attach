// Host implementation of the one Android symbol the runtime actually calls.
#include <android/log.h>
#include <cstdarg>
#include <cstdio>

extern "C" int __android_log_print(int, const char* tag, const char* fmt, ...) {
    std::printf("[%s] ", tag);
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    return 0;
}

#include "modules/ModuleManager.h"
// ── runtime pieces the agent touches but this host test does not need ──────
// A real (empty) module registry plus the Kotlin mirror seam, so the bridge can
// be exercised end-to-end without a device.
namespace eclient_runtime::modules {
ModuleManager& ModuleManager::instance() { static ModuleManager m; return m; }
ModuleManager::ModuleManager() = default;
const std::vector<ModuleState>& ModuleManager::all() const { return modules_; }
bool ModuleManager::setEnabled(const std::string&, bool) { return false; }
bool ModuleManager::enabled(const std::string&) const { return false; }
bool ModuleManager::setBoolSetting(const std::string&, const std::string&, bool) { return false; }
bool ModuleManager::setFloatSetting(const std::string&, const std::string&, float) { return false; }
bool ModuleManager::setIntSetting(const std::string&, const std::string&, int) { return false; }
bool ModuleManager::setEnumSetting(const std::string&, const std::string&, int) { return false; }
} // namespace eclient_runtime::modules

namespace eclient_runtime::kotlinbridge {
void setModuleEnabled(const char*, bool) {}
} // namespace eclient_runtime::kotlinbridge
