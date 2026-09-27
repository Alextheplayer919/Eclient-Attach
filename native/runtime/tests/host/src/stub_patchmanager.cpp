// stub_patchmanager.cpp — a FAITHFUL patch manager for the host harness.
//
// This used to return hasSpec()==false and specCount()==0 unconditionally, which
// made every memory-backed toggle REFUSE and made patchCoverageAudit() return
// early. The harness therefore could not observe the one bug class that reached
// the user twice ("toggling any module says patch profiles are not validated"),
// and could not test the fix either. It now behaves like the real thing:
//
//   * before initialize() the spec table is EMPTY, exactly like the real one
//     (this is what makes hasSpec() a trap for unbuilt profiles)
//   * after initialize() the table holds the same three memory-backed names the
//     real PatchManager.cpp does, and a write "succeeds"
//
// Tests flip g_stubProfileReady to model "the profile never validated".
#include "profile/PatchManager.h"

#include <array>
#include <cstring>

namespace eclient_runtime::profile {

// Harness control + observation counters (declared in the test TU).
bool g_stubProfileReady = true;
int  g_stubSetCalls = 0;          // how many times set() was reached
int  g_stubInitCalls = 0;

namespace {
// The memory-backed module names, mirroring PatchManager.cpp's spec table. Keep
// in sync with the real table — native/runtime/tests/host/check_module_patch_coverage.py
// is what makes a drift here fail a test instead of a phone.
constexpr std::array<const char*, 3> kSpecs = {"AntiKnockback", "NoSlowDown", "NoBlur"};
} // namespace

PatchManager& PatchManager::instance() { static PatchManager p; return p; }

bool PatchManager::initialize() {
    ++g_stubInitCalls;
    ready_ = g_stubProfileReady;
    status_ = g_stubProfileReady ? "host stub profile validated"
                                 : "host stub profile not validated";
    return ready_;
}

bool PatchManager::set(const std::string& module, bool enabled, std::string* why) {
    ++g_stubSetCalls;
    (void)enabled;
    if (!ready_ && !initialize()) {
        if (why) *why = "patch profile not validated (" + status_ + ")";
        return false;
    }
    if (!hasSpec(module)) {
        if (why) *why = "no patch profile named \"" + module + "\"";
        return false;
    }
    if (why) why->clear();
    return true;
}

bool PatchManager::enabled(const std::string&) const { return false; }

bool PatchManager::ready() const {
    // g_stubProfileReady is the source of truth, not the sticky member: a test
    // must be able to say "this profile never validated" AFTER a successful
    // initialize(), which is exactly the device state that produced the r43
    // refusal with the wrong reason.
    return g_stubProfileReady;
}

std::string PatchManager::status() const {
    return ready_ ? "host stub profile validated" : "host stub profile not validated";
}

bool PatchManager::hasSpec(const std::string& module) const {
    // An unvalidated profile has an empty spec table — the exact trap this stub
    // used to hide.
    if (!g_stubProfileReady) return false;
    for (const char* s : kSpecs) if (module == s) return true;
    return false;
}

std::size_t PatchManager::specCount() const { return g_stubProfileReady ? 18 : 0; }

} // namespace eclient_runtime::profile
