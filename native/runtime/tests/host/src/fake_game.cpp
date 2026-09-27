// Fake game layer: implements the melody API the runtime/agent uses, with a
// deterministic world so the bridge can be asserted against.
#include "modules/MelodyModules.h"
#include <cstring>
#include <string>

namespace fake {
int g_attackCalls = 0;
int g_lastAttackIndex = -1;
} // namespace fake

namespace echo {
int attackCalls() { return fake::g_attackCalls; }
int lastAttackIndex() { return fake::g_lastAttackIndex; }
} // namespace echo

namespace eclient_runtime::modules::melody {
namespace {
BotActor g_actors[3];
} // namespace

bool botSelf(void*, float* out) {
    // x, y, z, yaw, pitch, ?, health, ?, onGround, ping
    out[0] = 10; out[1] = 64; out[2] = -3;
    out[3] = 0;  out[4] = 0;  out[5] = 0;
    out[6] = 20; out[7] = 0;  out[8] = 1; out[9] = 12;
    return true;
}

int botScan(void*, float, int mode, BotActor* out, int cap) {
    g_actors[0] = BotActor{};
    g_actors[0].x = 12; g_actors[0].y = 64; g_actors[0].z = -3;
    g_actors[0].distance = 2; g_actors[0].health = 20; g_actors[0].typeId = 319;
    g_actors[0].entityId = 1001; g_actors[0].flags = kBotFlagAttackable;

    g_actors[1] = BotActor{};
    g_actors[1].x = 14; g_actors[1].y = 64; g_actors[1].z = -3;
    g_actors[1].distance = 4; g_actors[1].health = 18; g_actors[1].typeId = 319;
    g_actors[1].entityId = 1002; g_actors[1].flags = kBotFlagAttackable | kBotFlagHurt;

    g_actors[2] = BotActor{};
    g_actors[2].x = 1; g_actors[2].y = 63; g_actors[2].z = 1;
    g_actors[2].distance = 9; g_actors[2].health = 10; g_actors[2].typeId = 999;
    g_actors[2].entityId = 1003; g_actors[2].flags = kBotFlagAttackable;

    const int n = (mode == 0) ? 2 : 3;   // mode 0 = players only
    if (n > cap) return cap;
    std::memcpy(out, g_actors, sizeof(BotActor) * static_cast<std::size_t>(n));
    return n;
}

std::string botScanDiag() {
    return "scan(mode=1): actors=3 picked=3 | myType=0x13f(comp) ids=[0x13f,0x13f,0x3e7] fallback=no";
}

bool botAttackIndex(void*, int index) {
    fake::g_attackCalls++;
    fake::g_lastAttackIndex = index;
    return index >= 0 && index < 3;
}

bool botAimAt(void*, int, float* out) { out[0] = 90; out[1] = 0; return true; }
std::uint32_t botMyTypeId(bool* ok) { if (ok) *ok = true; return 319; }
bool botIsPlayerType(std::uint32_t t) { return t == 319; }
bool botApplyRotation(void*, float, float) { return false; }
bool botApplyInput(void*, float, float, bool, bool) { return false; }
bool botRotationAvailable() { return false; }
} // namespace eclient_runtime::modules::melody
