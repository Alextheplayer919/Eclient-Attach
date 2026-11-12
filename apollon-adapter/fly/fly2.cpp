#include "fly2.h"
#include <algorithm>
#include <cmath>

namespace eclient::fly2 {
void reset(State& s) { s = {}; }
Result plan(State& s, const Input& i, const Settings& c, Vec3& out) {
    auto finite = [](float v) { return std::isfinite(v); };
    if (!finite(i.position.x) || !finite(i.position.y) || !finite(i.position.z) ||
        !finite(i.strafe) || !finite(i.forward) || !finite(i.yawDegrees) ||
        !finite(c.horizontalBps) || !finite(c.upBps) || !finite(c.downBps) ||
        !finite(c.glide) || !finite(c.upHorizontalFactor) || !finite(c.downHorizontalFactor) ||
        c.horizontalBps < 1 || c.horizontalBps > 100 || c.upBps < 1 || c.upBps > 60 ||
        c.downBps < 1 || c.downBps > 60 || c.glide < -0.3f || c.glide > 0 ||
        c.upHorizontalFactor < 0.1f || c.upHorizontalFactor > 1 ||
        c.downHorizontalFactor < 0.1f || c.downHorizontalFactor > 1) {
        reset(s);
        return Result::invalid;
    }
    // Unlike the original's zero-vector initialization, never send motion on
    // the first observation: it safely establishes a baseline at any spawn.
    if (!s.hasLastPosition) { s.lastPosition = i.position; s.hasLastPosition = true; return Result::primed; }
    const double dx = double(i.position.x) - s.lastPosition.x;
    const double dy = double(i.position.y) - s.lastPosition.y;
    const double dz = double(i.position.z) - s.lastPosition.z;
    s.lastPosition = i.position;
    if (dx*dx + dy*dy + dz*dz > 4.5 * 4.5) return Result::positionJump;
    if (!i.up && !i.down && i.forward == 0 && i.strafe == 0 && c.glide == 0) return Result::idle;
    float speed = std::min(c.horizontalBps / 20.0f, std::sqrt(5.99f));
    if (i.up) speed *= c.upHorizontalFactor;
    if (i.down) speed *= c.downHorizontalFactor;
    const float vertical = i.up ? c.upBps / 20.0f : i.down ? -c.downBps / 20.0f + c.glide : c.glide;
    const double yaw = double(i.yawDegrees) * (3.14159265358979323846 / 180.0);
    const double x = -std::sin(yaw) * i.forward + std::cos(yaw) * i.strafe;
    const double z = std::cos(yaw) * i.forward + std::sin(yaw) * i.strafe;
    const double length = std::sqrt(x*x + z*z);
    out = length > 0.001 ? Vec3{float(x / length * speed), vertical, float(z / length * speed)} : Vec3{0, vertical, 0};
    return Result::motion;
}
}
