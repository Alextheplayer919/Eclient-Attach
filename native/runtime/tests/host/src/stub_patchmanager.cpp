// stub_patchmanager.cpp — no byte patches on the host.
//
// ModuleManager::setEnabled() delegates memory-backed modules to the patch
// manager. There is no libminecraftpe.so here to patch, so every patch is a
// no-op reporting success; the modules that matter for timing do not depend on
// patches (Fly goes through abilities, KillAura2 through its own reads).
#include "profile/PatchManager.h"

namespace eclient_runtime::profile {
PatchManager& PatchManager::instance() { static PatchManager p; return p; }
bool PatchManager::initialize() { return true; }
bool PatchManager::set(const std::string&, bool) { return true; }
bool PatchManager::enabled(const std::string&) const { return false; }
bool PatchManager::ready() const { return true; }
} // namespace eclient_runtime::profile
