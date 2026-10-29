#include "modules/GameplayModules.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"
#include "memory/NativeMemory.h"
#include "dobby.h"
#include "modules/flyx/flyx_core.h"
#include <android/log.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <time.h>

namespace eclient_runtime::modules::gameplay {
namespace {
constexpr const char* TAG = "EClientGameplay";
using namespace eclient_runtime::profile::mc_1_21_111;

using NormalTickFn = void* (*)(void*);
using SpeedFn = void (*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*);
using GetAbilitiesFn = void* (*)(void*);
using GetLayerFn = void* (*)(void*, int);
using SetAbilityFn = void (*)(void*, int, bool);
using GetRuntimeActorFn = std::vector<void*> (*)(void*);
using AttackFn = void (*)(void*, void*, bool);
using IsAttackableFn = bool (*)(void*);
using GetHealthFn = int (*)(void*);
using LerpMotionFn = void (*)(void*, const FlyXVec3*);

NormalTickFn oldNormalTick = nullptr;
SpeedFn oldSpeed = nullptr;
GetAbilitiesFn getAbilities = nullptr;
GetLayerFn getLayer = nullptr;
SetAbilityFn setAbility = nullptr;
GetRuntimeActorFn getRuntimeActors = nullptr;
AttackFn attack = nullptr;
IsAttackableFn isAttackable = nullptr;
GetHealthFn getHealth = nullptr;
LerpMotionFn lerpMotion = nullptr;
std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }
inline bool on(const char* n) { return ModuleManager::instance().enabled(n); }

// ── FlyX helpers — adapted from D4Cplus/Spiral, pinned to 1.21.111 offsets ─────────
extern "C" float spiral_flyx_sin(float a) { return sinf(a); }
extern "C" float spiral_flyx_cos(float a) { return cosf(a); }
extern "C" float spiral_flyx_sqrt(float v) { return sqrtf(v); }

static FlyXCore g_flyXCore{};
static bool g_flyXInitialized = false;
static uint64_t g_lastTickNs = 0;
static float g_upHFactor = 1.0f;
static float g_upVFactor = 1.0f;

static uint64_t clockNowNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Minimal EnTT component lookup — same layout as flyx_bindings.c, generic
static uintptr_t enttComponent(const void* player, uint32_t hash, unsigned stride) {
    uintptr_t actor = (uintptr_t)player;
    uintptr_t registry = *(uintptr_t*)(actor + 0x10);
    if (registry < 4096 || (registry & 7)) return 0;
    uintptr_t buckets = *(uintptr_t*)(registry + 0x38);
    uintptr_t bucketEnd = *(uintptr_t*)(registry + 0x40);
    uintptr_t nodes = *(uintptr_t*)(registry + 0x50);
    uintptr_t nodeEnd = *(uintptr_t*)(registry + 0x58);
    if (buckets < 4096 || (buckets & 7) || bucketEnd < buckets) return 0;
    if (nodes < 4096 || (nodes & 7) || nodeEnd < nodes) return 0;
    uintptr_t bucketCount = (bucketEnd - buckets) / 8;
    uintptr_t nodeCount = (nodeEnd - nodes) / 32;
    if (!bucketCount || (bucketCount & (bucketCount-1)) || !nodeCount) return 0;
    if (bucketCount > (1u<<20) || nodeCount > (1u<<20)) return 0;
    uintptr_t idx = *(uintptr_t*)(buckets + (hash & (bucketCount-1)) * 8);
    uintptr_t pool = 0;
    for (unsigned tries=0; tries<64 && idx < nodeCount; ++tries) {
        uintptr_t node = nodes + idx*32;
        if (*(uint32_t*)(node+8) == hash) { pool = *(uintptr_t*)(node+0x10); break; }
        idx = *(uintptr_t*)(node+0);
    }
    if (pool < 4096 || (pool & 7)) return 0;
    uint32_t entity = *(uint32_t*)(actor + 0x18);
    uintptr_t sparse = *(uintptr_t*)(pool+8);
    uintptr_t sparseEnd = *(uintptr_t*)(pool+0x10);
    if (sparse < 4096 || (sparse & 7) || sparseEnd < sparse) return 0;
    uintptr_t sparseCount = (sparseEnd - sparse)/8;
    if (sparseCount > 128) return 0;
    unsigned sparsePage = (entity >> 11) & 127;
    if (sparsePage >= sparseCount) return 0;
    uintptr_t sparseEntries = *(uintptr_t*)(sparse + sparsePage*8);
    if (sparseEntries < 4096 || (sparseEntries & 7)) return 0;
    uint32_t packed = *(uint32_t*)(sparseEntries + (entity & 2047)*4);
    if ((packed ^ (entity & 0xfffc0000u)) >= 0x3ffffu) return 0;
    if (!stride) return 1;
    uintptr_t pages = *(uintptr_t*)(pool+0x50);
    uintptr_t pagesEnd = *(uintptr_t*)(pool+0x58);
    if (pages < 4096 || (pages & 7) || pagesEnd < pages) return 0;
    uintptr_t pageCount = (pagesEnd - pages)/8;
    if (pageCount > 2048) return 0;
    unsigned page = (packed & 0x3ffffu) >> 7;
    if (page >= pageCount) return 0;
    uintptr_t data = *(uintptr_t*)(pages + page*8);
    if (data < 4096 || (data & 7)) return 0;
    return data + (packed & 127)*stride;
}

static bool buildFlyXInput(void* player, FlyXInput* out) {
    uintptr_t actor = (uintptr_t)player;
    uintptr_t move = enttComponent(player, MoveInputComponentHash, 160);
    if (!move) return false;
    // Prefer analog vectors, fall back to digital bytes — as in flyx_bindings snapshot
    float strafe = 0, forward = 0;
    bool hasAxes = false;
    // move+0x58 analog (already packet-motion), +0x3c raw, +0x14 analogMove
    auto axesOk = [](float x,float y){ return std::isfinite(x)&&std::isfinite(y)&&fabsf(x)<=1.001f&&fabsf(y)<=1.001f&&(fabsf(x)>0.04f||fabsf(y)>0.04f); };
    float x = *(float*)(move+0x58), y = *(float*)(move+0x58+4);
    if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; }
    if (!hasAxes) {
        x = *(float*)(move+0x3c); y = *(float*)(move+0x40);
        if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; }
    }
    if (!hasAxes) {
        x = *(float*)(move+0x14); y = *(float*)(move+0x14+4);
        if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; }
    }
    if (!hasAxes) {
        uint8_t fwd = *(uint8_t*)(move+0x0d), back = *(uint8_t*)(move+0x0e);
        uint8_t left = *(uint8_t*)(move+0x0f), right = *(uint8_t*)(move+0x10);
        if (fwd>1||back>1||left>1||right>1) return false;
        strafe = (float)((int)left - (int)right); // note: original was left-right? keep strafe as x
        // Actually original: strafe = left - right? Check flyx_bindings: strafe = (left - right) via 0xf - 0x10
        // forward = 0xd - 0xe (forward - back)
        forward = (float)((int)fwd - (int)back);
        // Re-map to consistent axes if no analog: we already set
        // If still zero, that's fine (not moving)
        if (fabsf(strafe)<=0.001f && fabsf(forward)<=0.001f) {
            strafe = 0; forward = 0;
        }
        // Digital fallback uses same convention; keep it
        // But original code set strafe = 0xf - 0x10, forward = 0xd - 0xe, so above is correct
    } else {
        // we already have strafe/forward
    }
    // Copy into FlyXInput (note: FlyXInput.strafe = x, forward = y)
    out->strafe = hasAxes ? strafe : ((float)((int)*(uint8_t*)(move+0x0f) - (int)*(uint8_t*)(move+0x10)));
    // actually override: if hasAxes false we already handled, but to avoid confusion:
    if (hasAxes) { out->strafe = strafe; out->forward = forward; }
    else {
        out->strafe = (float)((int)*(uint8_t*)(move+0x0f) - (int)*(uint8_t*)(move+0x10));
        out->forward = (float)((int)*(uint8_t*)(move+0x0d) - (int)*(uint8_t*)(move+0x0e));
    }

    uint8_t jump = *(uint8_t*)(move+0x98), sneak = *(uint8_t*)(move+0x94), inWaterFlag = *(uint8_t*)(move+0x99);
    if (jump>1||sneak>1||inWaterFlag>1) return false;
    out->jump = jump;
    out->sneak = sneak;
    out->touching_terrain = inWaterFlag || (enttComponent(player, 0xd5ee0bd8u, 0)!=0);

    uintptr_t posPtr = *(uintptr_t*)(actor + GetPositionField); // 0x208 StateVector*
    uintptr_t rotPtr = *(uintptr_t*)(actor + RotationPtrField); // 0x218 -> float* {yaw,pitch}
    if (posPtr < 4096 || (posPtr & 3) || rotPtr < 4096 || (rotPtr & 7)) return false;
    out->position_x = *(float*)(posPtr + 0);
    out->position_z = *(float*)(posPtr + 8);
    out->yaw_degrees = *(float*)(rotPtr + 4); // yaw at +4 per FlyX (previous yaw +12)
    // pos y not needed by planner except x/z for distance scaling
    if (!std::isfinite(out->position_x) || !std::isfinite(out->position_z) || !std::isfinite(out->yaw_degrees)) return false;
    if (!std::isfinite(out->strafe) || !std::isfinite(out->forward)) return false;
    return true;
}

// ── TargetLock ─────────────────────────────────────────────────────────────
static void applyTargetLock(void* player, void* target) {
    if (!player || !target) return;
    uintptr_t pPosPtr = *(uintptr_t*)((uintptr_t)player + GetPositionField);
    uintptr_t tPosPtr = *(uintptr_t*)((uintptr_t)target + GetPositionField);
    uintptr_t rotPtr  = *(uintptr_t*)((uintptr_t)player + RotationPtrField);
    if (pPosPtr < 4096 || tPosPtr < 4096 || rotPtr < 4096) return;
    float px = *(float*)(pPosPtr + 0), py = *(float*)(pPosPtr + 4), pz = *(float*)(pPosPtr + 8);
    float tx = *(float*)(tPosPtr + 0), ty = *(float*)(tPosPtr + 4), tz = *(float*)(tPosPtr + 8);
    float dx = tx - px, dy = (ty - py), dz = tz - pz;
    // Use eye height offset approx 1.62 like Apollon if available; fall back to raw delta
    // Apollon used hurtCheck style; we keep simple center aim
    float dist2d = sqrtf(dx*dx + dz*dz);
    if (dist2d < 0.001f) return;
    float yaw = atan2f(dz, dx) * 57.2957795f - 90.0f;
    float pitch = -atan2f(dy, dist2d) * 57.2957795f;
    // normalize yaw to -180..180
    while (yaw > 180.f) yaw -= 360.f;
    while (yaw < -180.f) yaw += 360.f;
    if (pitch > 90.f) pitch = 90.f;
    if (pitch < -90.f) pitch = -90.f;
    // Write to Actor rotation Vec2 {yaw,pitch}
    *(float*)(rotPtr + 0) = yaw;
    *(float*)(rotPtr + 4) = pitch;
    // Also drive body/head if helpers present
    if (base) {
        auto setBody = reinterpret_cast<void(*)(void*,float)>(fn(SetYBodyRot));
        auto setHead = reinterpret_cast<void(*)(void*,float)>(fn(SetYHeadRot));
        if (setBody) setBody(player, yaw);
        if (setHead) setHead(player, yaw);
    }
}

void* hookedNormalTick(void* player) {
    void* result = oldNormalTick ? oldNormalTick(player) : nullptr;
    if (!player) return result;

    if (on("AlwaysSprint")) {
        using Fn = void (*)(void*, bool);
        reinterpret_cast<Fn>(fn(SetSprinting))(player, true);
    }
    if (on("Step")) {
        using Fn = void (*)(void*, float);
        reinterpret_cast<Fn>(fn(InitMaxAutoStep))(player, 1.0625f);
    }

    if (on("Fly") && getAbilities && getLayer && setAbility) {
        void* abilities = getAbilities(player);
        void* layer = abilities ? getLayer(abilities, AbilityLayerBase) : nullptr;
        if (layer) {
            setAbility(layer, AbilityMayFly, true);
            setAbility(layer, AbilityFlying, true);
        }
    }

    // ── FlyX — bypass glide/bypass planner layered over the vanilla tick ──────
    if (on("FlyX") && lerpMotion) {
        uint64_t now = clockNowNs();
        // Reset on pause / world change detection
        if (!g_flyXInitialized || now < g_lastTickNs || now - g_lastTickNs > 500000000ull) {
            spiral_flyx_reset(&g_flyXCore);
            g_flyXInitialized = true;
        }
        g_lastTickNs = now;
        FlyXInput input{};
        input.now_ns = now;
        if (buildFlyXInput(player, &input)) {
            FlyXVec3 motion{};
            if (spiral_flyx_plan(&g_flyXCore, &input, g_upHFactor, g_upVFactor, &motion)) {
                lerpMotion(player, &motion);
            }
        }
    } else if (!on("FlyX")) {
        // Allow planner to reset cleanly next enable
        // keep core but will reset on next enable via time gap or explicit
    }

    // ── KillAura + optional TargetLock ───────────────────────────────────────
    if (on("KillAura") && getRuntimeActors && attack && isAttackable && getHealth) {
        void* level = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetLevelField);
        void* gameMode = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetGameModeField);
        void* posField = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetPositionField);
        if (level && gameMode && posField) {
            const float px = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField));
            const float py = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField) + 4);
            const float pz = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField) + 8);
            void* best = nullptr;
            float bestDist2 = 16.0f; // 4 blocks squared
            for (void* actor : getRuntimeActors(level)) {
                if (!actor || actor == player || !isAttackable(actor) || getHealth(actor) <= 0) continue;
                void* targetPos = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
                if (!targetPos) continue;
                const float dx = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)) - px;
                const float dy = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)+4) - py;
                const float dz = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)+8) - pz;
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < bestDist2) { bestDist2 = d2; best = actor; }
            }
            if (best) {
                if (on("TargetLock")) applyTargetLock(player, best);
                attack(gameMode, best, true);
            }
        }
    }
    return result;
}

void hookedSpeed(void* a1,void* a2,void* a3,void* a4,void* a5,void* a6,void* a7,void* a8,void* a9,void* a10,void* a11,void* a12,void* a13) {
    if (!oldSpeed) return;
    oldSpeed(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13);
}
} // anonymous

bool initialize() {
    if (base != 0) return true;
    memory::ModuleInfo info{};
    if (!memory::NativeMemory::instance().locateModule("libminecraftpe.so", info)) return false;
    base = info.base;

    if (GetAbilities) getAbilities = reinterpret_cast<GetAbilitiesFn>(fn(GetAbilities));
    if (GetLayer)     getLayer     = reinterpret_cast<GetLayerFn>(fn(GetLayer));
    if (SetAbility)   setAbility   = reinterpret_cast<SetAbilityFn>(fn(SetAbility));

    getRuntimeActors = reinterpret_cast<GetRuntimeActorFn>(fn(GetRuntimeActor));
    attack           = reinterpret_cast<AttackFn>(fn(Attack));
    isAttackable     = reinterpret_cast<IsAttackableFn>(fn(IsAttackable));
    getHealth        = reinterpret_cast<GetHealthFn>(fn(GetHealth));
    lerpMotion       = reinterpret_cast<LerpMotionFn>(fn(LerpMotion));

    if (DobbyHook(fn(NormalTick), reinterpret_cast<void*>(hookedNormalTick), reinterpret_cast<void**>(&oldNormalTick)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "NormalTick hook failed");
        return false;
    }
    if (DobbyHook(fn(SpeedHack), reinterpret_cast<void*>(hookedSpeed), reinterpret_cast<void**>(&oldSpeed)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Speed hook failed");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "1.21.111 gameplay hooks installed: Speed/Fly/FlyX/KillAura+TargetLock/Step/Sprint");
    return true;
}
void shutdown() {}
} // namespace eclient_runtime::modules::gameplay
