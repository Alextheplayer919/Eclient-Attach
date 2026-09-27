// bench_tick.cpp — what one tick of the REAL module code actually costs.
//
// Runs melody::onNormalTick() against the synthetic world in fake_world.cpp and
// prints microseconds per tick for each configuration, plus the work counters
// behind the time (actor list calls, actors returned, attacks, ability calls).
//
// The number to compare against: a 20 TPS tick has 50,000 us, and on Android
// the game thread also feeds the frame, so anything measured here is time the
// screen does not get.
#include "fake_world.h"

#include "bridge/AgentState.h"
#include "modules/MelodyModules.h"
#include "modules/ModuleManager.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using eclient_runtime::modules::ModuleManager;

namespace {

double bench(const char* label, int actorCount, bool killAura, bool fly, bool agentLive,
             long ticks) {
    fakeworld::setup(actorCount);
    auto& mgr = ModuleManager::instance();
    mgr.setEnabled("KillAura2", killAura);
    mgr.setEnabled("Fly", fly);

    eclient_runtime::agentstate::setLive(agentLive);

    // init + warm-up: the first ticks build caches, allocate, and would not
    // represent steady state.
    eclient_runtime::modules::melody::initialize(fakeworld::base());
    for (int i = 0; i < 50; ++i) {
        eclient_runtime::modules::melody::onNormalTick(fakeworld::player());
    }

    const bool enabledNow = mgr.enabled("KillAura2");
    const auto before = fakeworld::stats();
    if (getenv("BENCH_DEBUG")) {
        std::printf("      [dbg] enabled(KillAura2)=%d players=%zu\n", enabledNow,
                    fakeworld::actorPointers().size());
    }
    const auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < ticks; ++i) {
        eclient_runtime::modules::melody::onNormalTick(fakeworld::player());
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto after = fakeworld::stats();

    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / ticks;
    std::printf("  %-34s %8.1f us/tick   %5.1f%% of a 50 ms tick\n", label, us,
                us / 50000.0 * 100.0);
    std::printf("      actors=%d  actorLists/tick=%.1f  attacks/tick=%.2f  abilityCalls/tick=%.1f\n",
                actorCount,
                double(after.runtimeActorCalls - before.runtimeActorCalls) / ticks,
                double(after.attacks - before.attacks) / ticks,
                double(after.abilityCalls - before.abilityCalls) / ticks);
    fakeworld::teardown();
    return us;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // survive a crash
    std::printf("tick cost of the real module code (host, x86-64; phone is slower)\n");
    std::printf("═══════════════════════════════════════════════════════════════════\n");

    std::printf("\nbaseline: no modules, nothing connected\n");
    bench("idle", 0, false, false, false, 2000);
    bench("idle, 25 actors nearby", 25, false, false, false, 2000);

    std::printf("\nwith modules\n");
    bench("KillAura2", 25, true, false, false, 1000);
    bench("Fly", 25, false, true, false, 2000);
    bench("KillAura2 + Fly", 25, true, true, false, 1000);

    std::printf("\nwith the app connected (publish + executeQueued every tick)\n");
    bench("agent live", 0, false, false, true, 1000);
    bench("agent live, 25 actors", 25, false, false, true, 1000);
    bench("agent live + KillAura2, 25 actors", 25, true, false, true, 1000);

    std::printf("\nscaling: agent live, players nearby\n");
    for (int n : {5, 10, 25, 50}) {
        bench((std::string("agent live, ") + std::to_string(n) + " actors").c_str(), n,
              false, false, true, 800);
    }

    std::printf("\nscaling: KillAura2 + agent live\n");
    for (int n : {5, 10, 25, 50}) {
        bench((std::string("aura+agent, ") + std::to_string(n) + " actors").c_str(), n,
              true, false, true, 800);
    }

    std::printf("\n═══════════════════════════════════════════════════════════════════\n");
    std::printf("50,000 us = a whole 20 TPS tick. Anything above ~10%% is time the\n");
    std::printf("frame does not get.\n");

    // The in-game panel shows diagLine() — print the same string here so the
    // host can prove it is well-formed, and so a device screenshot has a
    // reference for what "healthy" looks like.
    ModuleManager::instance().setEnabled("KillAura2", true);
    fakeworld::setup(12);
    eclient_runtime::modules::melody::initialize(fakeworld::base());
    for (int i = 0; i < 80; ++i) {
        eclient_runtime::modules::melody::onNormalTick(fakeworld::player());
    }
    std::printf("\n════════ diagLine() — exactly what the in-game panel shows ════════\n");
    std::printf("%s\n", eclient_runtime::modules::melody::diagLine().c_str());
    return 0;
}
