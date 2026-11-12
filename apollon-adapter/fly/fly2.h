#pragma once

// Fly2Module.smali translation: calculation only, not hooked to the game.
// The caller owns state and must reset it when the player/world changes or the
// module is disabled. No game memory or networking is accessed here.
namespace eclient::fly2 {
struct Vec3 { float x, y, z; };
struct Input {
    Vec3 position;
    float strafe, forward, yawDegrees;
    bool up, down;
};
struct Settings {
    float horizontalBps = 46.0f;
    float upBps = 19.8f;
    float downBps = 46.0f;
    float glide = -0.02f;
    float upHorizontalFactor = 0.55f;
    float downHorizontalFactor = 0.55f;
};
struct State { Vec3 lastPosition{}; bool hasLastPosition = false; };
enum class Result { invalid, primed, positionJump, idle, motion };
void reset(State& state);
Result plan(State& state, const Input& input, const Settings& settings, Vec3& motion);
}
