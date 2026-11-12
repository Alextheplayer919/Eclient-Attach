#pragma once
// Game-independent planner. An engine adapter must validate and execute actions
// on the game thread; this file never changes blocks, inventory or entities.
#include <cmath>
#include <cstdint>
namespace eclient::combat {
struct Pos { int x, y, z; };
inline bool operator==(Pos a, Pos b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
enum class Block { unknown, air, obsidian, bedrock, solid, replaceable };
enum class Action { none, placeBase, placeCrystal, attackCrystal };
struct Settings {
    bool baseplate = false;
    float placeRange = 4.0f, breakRange = 4.0f, targetRange = 4.0f, minSelfDistance = 3.0f;
    unsigned delayTicks = 4, confirmationTimeoutTicks = 20;
};
// Adapter supplies a snapshot of one candidate site. Coordinates are integer
// block coordinates; distances are measured independently by the adapter.
struct Site {
    Pos base{};
    Block block = Block::unknown;
    bool spaceAboveClear = false, supportFaceAvailable = false;
    bool hasCrystal = false, crystalIsAttackable = false;
    bool targetEligible = false, hasObsidian = false, hasCrystals = false;
    float placementDistance = 0, crystalDistance = 0, targetDistance = 0, selfDistance = 0;
};
struct State { std::uint64_t lastActionTick = 0, pendingUntil = 0; Pos pendingBase{}; Action pending = Action::none; bool hasAction = false; };
struct Decision { Action action = Action::none; Pos base{}; };
inline void reset(State& s) { s = {}; }
inline bool finiteDistance(float value) { return std::isfinite(value) && value >= 0; }
inline bool valid(const Site& s, const Settings& c) {
    return s.targetEligible && finiteDistance(s.placementDistance) && finiteDistance(s.crystalDistance) &&
        finiteDistance(s.targetDistance) && finiteDistance(s.selfDistance) &&
        finiteDistance(c.placeRange) && finiteDistance(c.breakRange) && finiteDistance(c.targetRange) &&
        finiteDistance(c.minSelfDistance) && s.targetDistance <= c.targetRange && s.selfDistance >= c.minSelfDistance;
}
// Never chains an obsidian placement into crystal placement on the same tick:
// wait for an observed world update or timeout before issuing another action.
inline Decision plan(State& state, std::uint64_t tick, const Site& site, const Settings& cfg) {
    if (state.hasAction && tick < state.lastActionTick) reset(state); // world/tick rollover
    if (state.pending != Action::none) {
        bool confirmed = site.base == state.pendingBase &&
            ((state.pending == Action::placeBase && site.block == Block::obsidian) ||
             (state.pending == Action::placeCrystal && site.hasCrystal) ||
             (state.pending == Action::attackCrystal && !site.hasCrystal));
        if (!confirmed && tick < state.pendingUntil) return {};
        state.pending = Action::none;
    }
    if (!valid(site,cfg) || (state.hasAction && tick - state.lastActionTick < cfg.delayTicks)) return {};
    Action action = Action::none;
    if (site.hasCrystal) {
        if (site.crystalIsAttackable && site.crystalDistance <= cfg.breakRange) action = Action::attackCrystal;
    } else if (site.spaceAboveClear && site.placementDistance <= cfg.placeRange) {
        if ((site.block == Block::obsidian || site.block == Block::bedrock) && site.hasCrystals)
            action = Action::placeCrystal;
        else if (cfg.baseplate && site.hasObsidian && site.hasCrystals && site.supportFaceAvailable &&
                 (site.block == Block::air || site.block == Block::replaceable))
            action = Action::placeBase;
    }
    return {action, site.base};
}
// Call only after the adapter reports that the action was successfully queued.
inline void record(State& state, std::uint64_t tick, Decision decision, const Settings& cfg) {
    if (decision.action == Action::none) return;
    state.lastActionTick = tick; state.hasAction = true; state.pending = decision.action;
    state.pendingBase = decision.base;
    const auto timeout = cfg.confirmationTimeoutTicks ? cfg.confirmationTimeoutTicks : 1;
    state.pendingUntil = tick > UINT64_MAX - timeout ? UINT64_MAX : tick + timeout;
}
}
