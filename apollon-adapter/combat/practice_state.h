#pragma once
#include <cmath>
#include <cstdint>
namespace eclient::combat {
// Standalone practice damage bookkeeping only: no actor, renderer or hooks.
struct PracticeState { bool active = false, unlimitedTotems = true; float health = 20.0f; unsigned pops = 0, totems = 0; };
enum class PracticeHit { ignored, damaged, popped, defeated };
inline void spawn(PracticeState& s, unsigned totems, bool unlimited) { s = {true, unlimited, 20.0f, 0, totems}; }
inline void despawn(PracticeState& s) { s = {}; }
inline PracticeHit applyPracticeDamage(PracticeState& s, float damage) {
    if (!s.active || !std::isfinite(damage) || damage <= 0) return PracticeHit::ignored;
    if (damage < s.health) { s.health -= damage; return PracticeHit::damaged; }
    if (s.unlimitedTotems || s.totems > 0) {
        if (!s.unlimitedTotems) --s.totems;
        ++s.pops; s.health = 1.0f; return PracticeHit::popped;
    }
    s.health = 0; s.active = false; return PracticeHit::defeated;
}
}
