#pragma once
#include <cmath>
#include <limits>
namespace eclient::combat {
inline float crystalDistanceSquared(float x, float y, float z, float ax, float ay, float az) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az))
        return std::numeric_limits<float>::infinity();
    const float dx=ax-x, dy=ay-y, dz=az-z;
    return dx*dx+dy*dy+dz*dz;
}
inline bool closerCrystal(float distanceSquared, float bestSquared) {
    return std::isfinite(distanceSquared) && distanceSquared >= 0 && distanceSquared <= 16.0f && distanceSquared < bestSquared;
}
}
