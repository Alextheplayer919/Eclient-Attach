// stub_patchmanager.cpp — no byte patches on the host.
//
// ModuleManager::setEnabled() delegates memory-backed modules to the patch
// manager. There is no libminecraftpe.so here to patch, so every patch is a
// no-op. hasSpec()/specCount() report an EMPTY profile on purpose: the audit in
// ModuleManager::patchCoverageAudit() returns early when the profile has not
// been built, so an empty table here can never produce false alarms.
#include "profile/PatchManager.h"

namespace eclient_runtime::profile {
PatchManager& PatchManager::instance() { static PatchManager p; return p; }
bool PatchManager::initialize() { return true; }
bool PatchManager::set(const std::string&, bool, std::string* why) {
    if (why) why->clear();
    return true;
}
bool PatchManager::enabled(const std::string&) const { return false; }
bool PatchManager::ready() const { return true; }
bool PatchManager::hasSpec(const std::string&) const { return false; }
std::size_t PatchManager::specCount() const { return 0; }
} // namespace eclient_runtime::profile
