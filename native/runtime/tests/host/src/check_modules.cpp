// check_modules.cpp — the module toggle matrix, through the REAL ModuleManager.
//
// Why this exists: the user reported "essentially no module works, not even the
// killaura" after a build where the harness was green. The harness was green
// because its PatchManager stub made every memory-backed toggle refuse and no
// test ever looked at one. This test looks at every module:
//
//   1. every module in the table toggles on and off, and the reported state
//      actually follows
//   2. a memory-backed module on a VALIDATED profile applies (no refusal)
//   3. a memory-backed module on an UNBUILT profile reports the validation
//      status — never "no patch profile named X", which is the message that
//      accuses this table of a wiring bug it does not have (r43 regression)
//   4. toggling Fly never touches the patch layer at all (it is not a patch
//      module, and pretending it was is what broke every toggle in r42)
//   5. an unknown module is refused, by name
#include <cstdio>
#include <string>

#include "modules/ModuleManager.h"
#include "profile/PatchManager.h"

using eclient_runtime::modules::ModuleManager;

// Counters + control living in stub_patchmanager.cpp.
namespace eclient_runtime::profile {
extern bool g_stubProfileReady;
extern int  g_stubSetCalls;
extern int  g_stubInitCalls;
} // namespace eclient_runtime::profile

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::printf("  %-62s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

static bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    using eclient_runtime::profile::g_stubProfileReady;

    auto& mgr = ModuleManager::instance();
    const auto& all = mgr.all();
    std::printf("module toggle matrix — %zu modules in the table\n", all.size());

    if (all.empty()) {
        std::string why;
        check(!mgr.setEnabled("NoSuchModule", true, &why) && contains(why, "unknown module"),
              "empty module foundation refuses unregistered modules");
        std::printf("  ──────────────────────────────────────────────────────────────\n");
        return failures == 0 ? 0 : 1;
    }

    // ── 1. every module toggles, both ways, on a validated profile ──────────
    g_stubProfileReady = true;
    for (const auto& m : all) {
        std::string why;
        const bool on = mgr.setEnabled(m.name, true, &why);
        check(on && why.empty() && mgr.enabled(m.name),
              m.name + "  on   → applied" + (why.empty() ? "" : " [" + why + "]"));

        std::string why2;
        const bool off = mgr.setEnabled(m.name, false, &why2);
        check(off && why2.empty() && !mgr.enabled(m.name),
              m.name + "  off  → applied" + (why2.empty() ? "" : " [" + why2 + "]"));
    }

    // ── 2. memory-backed modules apply rather than refuse ───────────────────
    for (const auto& m : all) {
        if (!m.memoryBacked) continue;
        std::string why;
        const bool on = mgr.setEnabled(m.name, true, &why);
        check(on && why.empty(), m.name + " (memory-backed) applies on a validated profile");
        mgr.setEnabled(m.name, false, nullptr);
    }

    // ── 3. an UNBUILT profile must blame validation, not the module table ───
    for (const auto& m : all) {
        if (!m.memoryBacked) continue;
        g_stubProfileReady = false;          // models "the profile never validated"
        const int setsBefore = eclient_runtime::profile::g_stubSetCalls;
        const int initsBefore = eclient_runtime::profile::g_stubInitCalls;
        std::string why;
        const bool on = mgr.setEnabled(m.name, true, &why);
        const bool lazyInitRan =
            eclient_runtime::profile::g_stubInitCalls > initsBefore ||
            eclient_runtime::profile::g_stubSetCalls > setsBefore;
        check(!on && contains(why, "not validated") && !contains(why, "no patch profile"),
              m.name + " on an unbuilt profile names validation, not a wiring bug");
        check(lazyInitRan, m.name + " rebuilt the profile instead of judging an empty one");
        g_stubProfileReady = true;
        mgr.setEnabled(m.name, false, nullptr);
    }

    // ── 4. Fly is ability-driven: the patch layer must never see it ─────────
    {
        const int before = eclient_runtime::profile::g_stubSetCalls;
        std::string why;
        const bool on = mgr.setEnabled("Fly", true, &why);
        check(on && why.empty(), "Fly applies (it writes no bytes)");
        check(eclient_runtime::profile::g_stubSetCalls == before,
              "Fly never reaches the patch layer");
        std::string why2;
        check(mgr.setEnabled("Fly", false, &why2) && why2.empty(), "Fly turns back off");
    }

    // ── 5. unknown module ───────────────────────────────────────────────────
    {
        std::string why;
        const bool ok = mgr.setEnabled("NoSuchModule", true, &why);
        check(!ok && contains(why, "unknown module"), "unknown module is refused by name");
    }

    std::printf("  ──────────────────────────────────────────────────────────────\n");
    if (failures == 0) {
        std::printf("  toggle matrix: all checks passed\n");
        return 0;
    }
    std::printf("  toggle matrix: %d FAILED\n", failures);
    return 1;
}
