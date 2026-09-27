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
#include <cstdio>
#include <vector>

int main() {
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
    return 0;
}
