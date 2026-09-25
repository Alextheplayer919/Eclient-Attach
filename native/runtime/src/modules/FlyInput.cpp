#include "modules/FlyInput.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace eclient_runtime::flyinput {
namespace {
std::mutex g_mutex;
Buttons g_rects;
// pointerId -> 1 (UP button) / 2 (DOWN button). Presence implies consume.
std::unordered_map<int, int> g_roles;

bool inside(float x, float y, float cx, float cy, float dia) {
    const float r = dia * 0.5f;
    const float dx = x - cx, dy = y - cy;
    return (dx * dx + dy * dy) <= r * r;
}
} // namespace

void setRects(const Buttons& b) {
    std::lock_guard lock(g_mutex);
    g_rects = b;
}

Buttons rects() {
    std::lock_guard lock(g_mutex);
    return g_rects;
}

void clearRects() {
    std::lock_guard lock(g_mutex);
    g_rects = {};
    g_roles.clear();
}

bool touch(int pointerId, int action, float x, float y) {
    std::lock_guard lock(g_mutex);
    if (!g_rects.valid) return false;

    if (action == 0) { // down: claim a button if it starts inside one
        if (inside(x, y, g_rects.upCx, g_rects.upCy, g_rects.dia)) {
            g_roles[pointerId] = 1;
            return true;
        }
        if (inside(x, y, g_rects.downCx, g_rects.downCy, g_rects.dia)) {
            g_roles[pointerId] = 2;
            return true;
        }
        return false;
    }

    const auto it = g_roles.find(pointerId);
    if (it == g_roles.end()) return false;
    if (action == 2) g_roles.erase(it); // up/cancel: release this pointer
    // Moves (and the final up) of a claimed pointer stay consumed — same
    // gesture-latch the toggle bar uses, so camera never sees them.
    return true;
}

bool heldUp() {
    std::lock_guard lock(g_mutex);
    for (const auto& [pid, role] : g_roles) if (role == 1) return true;
    return false;
}

bool heldDown() {
    std::lock_guard lock(g_mutex);
    for (const auto& [pid, role] : g_roles) if (role == 2) return true;
    return false;
}

void reset() {
    std::lock_guard lock(g_mutex);
    g_roles.clear();
}

} // namespace eclient_runtime::flyinput
