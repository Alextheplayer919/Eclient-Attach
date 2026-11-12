#pragma once
#include "fly2.h"
#include <cmath>
#include <cstdint>

// Game-independent bridge for a future *verified* native input reader.
// This is deliberately not connected to game memory yet.
namespace eclient::fly2 {
enum class CalculationStatus { unavailable, invalid, primed, positionJump, idle, motion };
struct NativeSnapshot {
    std::uintptr_t playerIdentity = 0;
    // The caller must only assert this after validating the native layout for
    // the exact game binary and input mode, not merely checking value ranges.
    bool layoutVerified = false;
    Input input{};
};
struct CalculationState { std::uintptr_t playerIdentity = 0; State movement{}; };
inline void reset(CalculationState& state) { state = {}; }
inline CalculationStatus calculate(CalculationState& state, bool enabled, bool gameReady,
                                   const NativeSnapshot& snapshot, const Settings& settings,
                                   Vec3& output) {
    if (!enabled || !gameReady || !snapshot.playerIdentity || !snapshot.layoutVerified) {
        reset(state);
        return CalculationStatus::unavailable;
    }
    if (state.playerIdentity != snapshot.playerIdentity) {
        reset(state);
        state.playerIdentity = snapshot.playerIdentity;
    }
    // Bad or uninitialized analog values must never produce full-speed motion.
    if (!std::isfinite(snapshot.input.strafe) || !std::isfinite(snapshot.input.forward) ||
        std::fabs(snapshot.input.strafe) > 1.001f || std::fabs(snapshot.input.forward) > 1.001f) {
        reset(state);
        return CalculationStatus::invalid;
    }
    switch (plan(state.movement, snapshot.input, settings, output)) {
    case Result::invalid: reset(state); return CalculationStatus::invalid;
    case Result::primed: return CalculationStatus::primed;
    case Result::positionJump: return CalculationStatus::positionJump;
    case Result::idle: return CalculationStatus::idle;
    case Result::motion: return CalculationStatus::motion;
    }
    reset(state);
    return CalculationStatus::invalid;
}
}
