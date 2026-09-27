// fake_world.h — a synthetic Minecraft world good enough to run the REAL
// module code on a plain Linux box.
//
// WHY THIS EXISTS
//   "The game lags" is not a measurement. This builds a world that satisfies
//   everything MelodyModules.cpp actually reads — the local player, its state
//   vector, the ECS registry with a real ActorType pool, and N actors — and
//   puts the game's own functions at the offsets the profile header claims, so
//   the module code can be run and TIMED without a phone.
//
// HOW THE FUNCTION TRICK WORKS
//   MelodyModules resolves its API as base + offset and calls it. So: mmap a
//   big arena, pretend it is libminecraftpe.so's base, and write a 12-byte
//   absolute-jump trampoline at every offset the modules use, pointing at a
//   C++ stub. The modules then call our stubs as if they were the game.
#pragma once

#include <cstdint>
#include <vector>

namespace fakeworld {

// Build (or rebuild) the world with `actorCount` actors. Safe to call again.
void setup(int actorCount);
void teardown();

std::uintptr_t base();      // the fake "libminecraftpe.so" base
void* player();             // the local player block
const std::vector<void*>& actorPointers();

// Counters, so the bench can show work per tick instead of just time.
struct Stats {
    long runtimeActorCalls = 0;   // Level::getRuntimeActorList
    long actorPointersReturned = 0;
    long attacks = 0;
    long abilityCalls = 0;
};
Stats& stats();

} // namespace fakeworld
