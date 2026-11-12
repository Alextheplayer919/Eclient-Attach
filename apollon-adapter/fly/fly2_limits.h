#pragma once
#include <cmath>
namespace eclient::fly2 {
struct Limits { float horizontalBps = 46.0f, upBps = 19.8f, downBps = 46.0f; };
// Limit only existing velocity: never manufacture movement or modify hover.
inline bool capVelocity(float& x, float& y, float& z, const Limits& limits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::fabs(x) > 100 || std::fabs(y) > 100 || std::fabs(z) > 100 ||
        !std::isfinite(limits.horizontalBps) || !std::isfinite(limits.upBps) || !std::isfinite(limits.downBps) ||
        limits.horizontalBps < 1 || limits.horizontalBps > 100 || limits.upBps < 1 || limits.upBps > 60 ||
        limits.downBps < 1 || limits.downBps > 60) return false;
    bool changed = false;
    const float horizontal = std::hypot(x, z), maxHorizontal = limits.horizontalBps / 20.0f;
    if (horizontal > maxHorizontal) { const float f = maxHorizontal / horizontal; x *= f; z *= f; changed = true; }
    const float maxUp = limits.upBps / 20.0f, maxDown = limits.downBps / 20.0f;
    if (y > maxUp) { y = maxUp; changed = true; }
    if (y < -maxDown) { y = -maxDown; changed = true; }
    return changed;
}
}
