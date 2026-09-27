// Why the memory layer was the lag.
//
// Times NativeMemory::read() — the REAL implementation — against a synthetic
// /proc/self/maps-free workload, and reports what the old per-read maps parse
// cost. The old number was measured at 146 us/read with 1250 mappings; this
// bench prints the current cost of the same access pattern so a regression is
// obvious immediately.
#include "memory/NativeMemory.h"

#include <chrono>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <vector>

// ── correctness: a freshly mapped region must be readable FIRST TRY ────────
// This is the regression test for the r38 refresh policy. The first version
// rate-limited refreshes and answered "unreadable" in between, so the moment
// the game mapped something new, every read into it failed for up to 250 ms —
// scans and position reads silently returned nothing. Do not "optimise" the
// refresh path without this test staying green.
static int checkFreshMapping() {
    const long page = sysconf(_SC_PAGESIZE);
    void* fresh = mmap(nullptr, static_cast<std::size_t>(page) * 4,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fresh == MAP_FAILED) {
        std::printf("fresh-mapping test: mmap failed\n");
        return 1;
    }
    volatile std::uintptr_t* cell = static_cast<std::uintptr_t*>(fresh);
    *cell = 0xEC1100D5u;

    auto& mem = eclient_runtime::memory::NativeMemory::instance();
    std::uintptr_t got = 0;
    const bool ok = mem.read(reinterpret_cast<std::uintptr_t>(fresh), &got, sizeof(got));
    std::printf("freshly mapped region readable on FIRST read: %s (value=%#lx)\n",
                ok && got == 0xEC1100D5u ? "yes" : "NO", static_cast<unsigned long>(got));

    // and an address that is genuinely unmapped must still be refused, repeatedly
    void* reclaimed = MAP_FAILED;
    {
        void* probe = mmap(nullptr, static_cast<std::size_t>(page), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (probe != MAP_FAILED) {
            munmap(probe, static_cast<std::size_t>(page));
            reclaimed = probe;
        }
    }
    bool refused = true;
    if (reclaimed != MAP_FAILED) {
        for (int i = 0; i < 5; ++i) {
            if (mem.read(reinterpret_cast<std::uintptr_t>(reclaimed), &got, sizeof(got))) refused = false;
        }
    }
    std::printf("unmapped address refused every time        : %s\n\n", refused ? "yes" : "NO");

    munmap(fresh, static_cast<std::size_t>(page) * 4);
    return (ok && got == 0xEC1100D5u && refused) ? 0 : 1;
}

int main() {
    int failures = checkFreshMapping();
    auto& mem = eclient_runtime::memory::NativeMemory::instance();
    std::uintptr_t probe = 0;
    const std::uintptr_t valid = reinterpret_cast<std::uintptr_t>(&probe);
    volatile std::uintptr_t sink = 0;

    // Warm the map table the way the first tick would.
    std::uintptr_t value = 0;
    mem.read(valid, &value, sizeof(value));
    std::printf("map table ranges cached: %zu\n\n", mem.cachedRangeCount());

    constexpr int kIterations = 200000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        if (mem.read(valid, &value, sizeof(value))) sink += value;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIterations;

    const auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) {
        if (mem.read(0x1, &value, sizeof(value))) sink += value;   // bad address
    }
    const auto t3 = std::chrono::steady_clock::now();
    const double ns_bad = std::chrono::duration<double, std::nano>(t3 - t2).count() / 20000;

    std::printf("read, cached   : %9.1f ns   (before r38: 146000.0 ns)\n", ns);
    std::printf("read, bad addr : %9.1f ns\n\n", ns_bad);

    const struct { const char* what; double reads; } loads[] = {
        {"one botScan   ", 1240},
        {"one combat tick", 3000},
        {"worst case     ", 6000},
    };
    std::printf("              %s   %10s   %10s\n", "reads", "before r38", "now");
    for (const auto& l : loads) {
        std::printf("  %s %6.0f    %7.1f ms    %6.3f ms\n", l.what, l.reads,
                    l.reads * 146000.0 / 1e6, l.reads * ns / 1e6);
    }
    std::printf("\n(20 TPS budget is 50 ms per tick.)\n");
    (void)sink;
    if (failures) std::printf("\n*** CORRECTNESS CHECK FAILED ***\n");
    return failures;
}
