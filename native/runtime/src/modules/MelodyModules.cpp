#include "modules/MelodyModules.h"

#include "memory/ComponentWalk.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Melody (1.20.51, Windows x64) module ports for E-Client 1.21.111.
//
// Port rules (user directive):
//   * logic / settings / math port 1:1 — offsets never transfer between builds;
//     every offset used here was re-derived from our own libminecraftpe.so
//     (disasm evidence lives in profile/Offsets_1_21_111.h).
//   * features whose memory is not derived yet fail INERT, staying in the GUI
//     with exact parity to the Melody settings instead of being stripped.
//   * Melody's `if (Mode = 1)` typo bug: port intended Single/Multi behavior.
// ============================================================================

namespace eclient_runtime::modules::melody {
namespace {
constexpr const char* TAG = "EClientMelody";
using namespace eclient_runtime::profile::mc_1_21_111;

using GetRuntimeActorFn = std::vector<void*> (*)(void* level);
using AttackFn = void (*)(void* gameMode, void* target, bool);
using IsAttackableFn = bool (*)(void* actor);
using GetHealthFn = int (*)(void* actor);
using GetEntityTypeIdFn = std::uint32_t (*)(void* actor);

GetRuntimeActorFn getRuntimeActors = nullptr;
AttackFn attack = nullptr;
IsAttackableFn isAttackable = nullptr;
GetHealthFn getHealth = nullptr;
GetEntityTypeIdFn getEntityTypeId = nullptr;

std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }

ModuleManager& mgr() { return ModuleManager::instance(); }

// ---------------------------------------------------------------------------
// Shared actor helpers (offsets from Offsets_1_21_111.h, disasm-proven)
// ---------------------------------------------------------------------------

struct Vec3 { float x, y, z; };

bool getPosition(void* actor, Vec3& out) {
    if (!actor) return false;
    void* sv = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
    if (!sv) return false;
    const auto p = static_cast<float*>(sv); // pos at +0x0 (LerpMotion evidence)
    out = {p[0], p[1], p[2]};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

Vec3* velocityPtr(void* actor) {
    if (!actor) return nullptr;
    void* sv = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
    if (!sv) return nullptr;
    // velocity Vec3 at stateVector + VelocityField (0x18) — LerpMotion evidence.
    return reinterpret_cast<Vec3*>(static_cast<char*>(sv) + VelocityField);
}

std::uint32_t entityIdOf(void* actor) {
    return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(actor) + EntityIdField);
}

// ============================================================================
// KillAura2 — full Melody KillAura port (source: MelodyV2 Modules/Combat/
// Killaura.cpp + Utils/TargetUtils.cpp; settings cloned 1:1 into ModuleManager)
// ============================================================================

// Melody TargetUtils::isTargetValid hardcoded "not attackable" entity ids.
constexpr std::uint32_t kInvalidTypeIds[] = {
    0, 64, 66, 69, 70, 95, 4194372, 4194390, 4194391, 4194405, 12582992
};

bool typeIdBlocked(std::uint32_t id) {
    for (std::uint32_t blocked : kInvalidTypeIds) if (blocked == id) return true;
    return false;
}

// Melody GetRotations (verbatim math):
//   yaw   = atan2(dz, dx) * 180/pi - 90
//   pitch = -atan2(dy, sqrt(dx^2+dz^2)) * 180/pi
struct Rot { float yaw, pitch; };

Rot getRotations(const Vec3& from, const Vec3& to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    static constexpr float DEG = 180.0f / 3.14159265358979323846f;
    Rot r{};
    r.yaw = std::atan2(dz, dx) * DEG - 90.0f;
    r.pitch = -std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * DEG;
    if (r.yaw > 180.0f) r.yaw -= 360.0f;
    if (r.yaw < -180.0f) r.yaw += 360.0f;
    return r;
}

// Hurt-state tracking: Melody read target->hurtTime directly. We approximate
// the same idea without that field by watching GetHealth for drops; a drop
// marks 10 ticks (0.5 s vanilla invulnerability) of "recently hurt".
struct HurtState {
    int health{0};
    long hurtUntil{0};
};

std::unordered_map<void*, HurtState> g_hurtState;
long g_tick = 0;
int g_attackTimer = 0;

bool recentlyHurt(void* actor, int health) {
    auto it = g_hurtState.find(actor);
    if (it == g_hurtState.end()) {
        g_hurtState.emplace(actor, HurtState{health, 0});
        return false;
    }
    HurtState& st = it->second;
    if (health < st.health) st.hurtUntil = g_tick + 10; // damage happened just now
    st.health = health;
    return g_tick < st.hurtUntil;
}

bool killauraTargetValid(void* player, void* actor, const Vec3& myPos, float range,
                         bool playersOnly, std::uint32_t myTypeId) {
    if (!actor || actor == player) return false;
    if (!isAttackable(actor)) return false;
    const int health = getHealth(actor);
    if (health <= 0) return false;
    if (getEntityTypeId) {
        const std::uint32_t id = getEntityTypeId(actor);
        if (typeIdBlocked(id)) return false;
        if (playersOnly && myTypeId != 0 && id != myTypeId) return false;
    }
    Vec3 pos{};
    if (!getPosition(actor, pos)) return false;
    const float dx = pos.x - myPos.x;
    const float dy = pos.y - myPos.y;
    const float dz = pos.z - myPos.z;
    return (dx * dx + dy * dy + dz * dz) <= range * range;
}

// KillAura2 tick. Settings (Melody parity) are read through ModuleManager:
//   Range 1..12 / WallRange 0..12 / Mode Single|Multi
//   Rotation None|Silent|Strafe / Switch None|Full|Silent / Hurttime / Delay 0..20
void tickKillAura2(void* player) {
    if (!getRuntimeActors || !attack || !isAttackable || !getHealth) return;

    const float range = mgr().floatSetting("KillAura2", "Range", 3.0f);
    float wallRange = mgr().floatSetting("KillAura2", "WallRange", 0.0f);
    if (wallRange <= 0.0f) wallRange = range; // 0 = same as Range (LOS not ported yet)
    const bool multi = mgr().enumSetting("KillAura2", "Mode", 0) == 1;
    const int rotationMode = mgr().enumSetting("KillAura2", "Rotation", 0);
    // Switch mode needs inventory access (still being derived): acts as None.
    (void)mgr().enumSetting("KillAura2", "Switch", 0);
    const bool useHurttime = mgr().boolSetting("KillAura2", "Hurttime", false);
    const int delay = mgr().intSetting("KillAura2", "Delay", 2);
    const bool playersOnly = mgr().boolSetting("KillAura2", "PlayersOnly", false);

    void* level = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetLevelField);
    void* gameMode = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetGameModeField);
    Vec3 myPos{};
    if (!level || !gameMode || !getPosition(player, myPos)) return;

    const float effectiveRange = std::max(range, wallRange);
    const std::uint32_t myTypeId = (getEntityTypeId && playersOnly) ? getEntityTypeId(player) : 0;

    // Gather + sort targets Melody-style (nearest first).
    std::vector<std::pair<float, void*>> targets;
    for (void* actor : getRuntimeActors(level)) {
        if (!killauraTargetValid(player, actor, myPos, effectiveRange, playersOnly, myTypeId)) continue;
        if (useHurttime && recentlyHurt(actor, getHealth(actor))) continue;
        Vec3 pos{};
        getPosition(actor, pos);
        const float dx = pos.x - myPos.x, dy = pos.y - myPos.y, dz = pos.z - myPos.z;
        targets.emplace_back(dx * dx + dy * dy + dz * dz, actor);
    }
    std::sort(targets.begin(), targets.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    if (++g_attackTimer <= delay) return;
    if (targets.empty()) return;
    g_attackTimer = 0;

    const int count = multi ? static_cast<int>(targets.size()) : 1;
    for (int i = 0; i < count; ++i) {
        void* target = targets[static_cast<std::size_t>(i)].second;
        Vec3 targetPos{};
        if (!getPosition(target, targetPos)) continue;

        // Melody applies Fruit rotations (math ported verbatim above).
        // Rotation modes need the ActorRotationComponent element address;
        // until ECSDump discovers it they intentionally fail inert.
        const Rot rot = getRotations(myPos, targetPos);
        (void)rot;
        if (rotationMode != 0 && ActorRotationComponentHash != 0) {
            using namespace eclient_runtime::ecs;
            const ElementInfo el = resolveElement(
                [player]() -> std::uintptr_t {
                    std::uintptr_t registry = *reinterpret_cast<std::uintptr_t*>(
                        reinterpret_cast<std::uintptr_t>(player) + EcsRegistryField);
                    if (!registry) return 0;
                    for (const PoolDesc& d : listPools(reinterpret_cast<std::uintptr_t>(player)))
                        if (d.typeHash == ActorRotationComponentHash) return d.pool;
                    return 0;
                }(),
                entityIdOf(player));
            if (el.valid) {
                auto* rotField = reinterpret_cast<Vec3*>(
                    el.block + el.inBlock * ActorRotationStride + ActorRotationYawOffset);
                float* yawPitch = reinterpret_cast<float*>(rotField); // Vec2 {yaw, pitch}
                if (std::isfinite(rot.yaw) && std::isfinite(rot.pitch)) {
                    yawPitch[0] = rot.yaw;   // Strafe memory-write rotation
                    yawPitch[1] = rot.pitch;
                }
            }
        }

        attack(gameMode, target, true);
    }
}

// ============================================================================
// Fly — native port of the proxy's MotionFly ("LeHu-style") feel.
//
// Proxy MotionFly does a full 3D motion override per outgoing packet: analog
// stick projected over camera yaw at HSpeed BPS, vertical = up-speed while
// JUMP held / -down+glide while SNEAK held / glide otherwise, plus an
// anti-rubberband position jump skip. Camera yaw and the jump/sneak flags
// live in the PlayerAuthInputPacket the proxy reads; our in-process
// equivalents (rotation component, input flag components) are still being
// discovered via ECSDump, so the mobile-side adaptation keeps MotionFly's
// BEHAVIOUR with observable state only:
//   * horizontal: your vanilla momentum direction IS yaw-projected analog
//     input already; we rescale its magnitude to HSpeed * (stick tilt ratio)
//     so a light touch creeps and a full push sprints — same as the proxy.
//   * vertical: glide default exactly like MotionFly; Jetpack mode detects
//     real jump impulses (vy edge past ~0.3) and sustains UpSpeed for a
//     short window — tap-jump to climb; hold-to-ascend unlocks with the
//     jump-flag component once discovered.
// ============================================================================

Vec3 g_lastFlyPos{};
bool g_haveLastFlyPos = false;
int g_jetSustainTicks = 0;
float g_prevVy = 0.0f;

void tickFly(void* player) {
    Vec3* vel = velocityPtr(player);
    if (!vel) return;

    float vx = vel->x, vy = vel->y, vz = vel->z;
    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return;

    // Plausibility gate on ALL derived-layout writes (per port policy).
    if (std::fabs(vx) > 100.0f || std::fabs(vy) > 100.0f || std::fabs(vz) > 100.0f) return;

    // MotionFly anti-rubberband: skip a tick where the server teleported us
    // (>4.5 blocks in one tick) so we never fight position corrections.
    Vec3 pos{};
    if (!getPosition(player, pos)) return;
    if (g_haveLastFlyPos) {
        const float dx = pos.x - g_lastFlyPos.x;
        const float dy = pos.y - g_lastFlyPos.y;
        const float dz = pos.z - g_lastFlyPos.z;
        if ((dx * dx + dy * dy + dz * dz) > 4.5f * 4.5f) {
            g_lastFlyPos = pos;
            return;
        }
    }
    g_haveLastFlyPos = true;
    g_lastFlyPos = pos;

    const float horizBps = mgr().floatSetting("Fly", "HSpeed BPS", 46.0f);
    const float glide = mgr().floatSetting("Fly", "Glide", -0.02f);
    const float upBps = mgr().floatSetting("Fly", "UpSpeed BPS", 19.8f);
    const float upHFactor = mgr().floatSetting("Fly", "Up H Factor", 0.55f);
    const int verticalMode = mgr().enumSetting("Fly", "VerticalMode", 2);

    // Horizontal — MotionFly computes horizSpeed = min(BPS/20, sqrt(5.99)).
    float horizSpeed = std::min(horizBps / 20.0f, 2.4449f);
    // LeHu: horizontal is slowed by Up H Factor while ascending.
    if (g_jetSustainTicks > 0) horizSpeed *= upHFactor;

    const float mag = std::sqrt(vx * vx + vz * vz);
    // Vanilla full-stick sprint momentum ≈ 0.28 (sprint) — treat that as the
    // analog "1.0" so stick tilt scales speed; below ~0.02 there is no input.
    if (mag > 0.02f) {
        constexpr float FULL_STICK_MAG = 0.28f;
        const float ratio = std::min(mag / FULL_STICK_MAG, 1.0f);
        const float target = horizSpeed * ratio;
        const float scale = target / mag;
        vx *= scale;
        vz *= scale;
    }
    // No else: with no stick input vanilla friction already damps to a stop,
    // matching MotionFly (input 0 → motion 0).

    // Vertical.
    switch (verticalMode) {
    case 2: // Jetpack: glide base + impulse-sustained climb on real jumps
    {
        // Edge detect a jump impulse: vy was low/negative, now strongly up.
        if (vy > 0.30f && g_prevVy <= 0.10f && vy < 2.0f) {
            g_jetSustainTicks = 10; // ~0.5 s of climb per tap
        }
        if (g_jetSustainTicks > 0) {
            vy = std::min(upBps / 20.0f, 2.4449f);
            --g_jetSustainTicks;
        } else {
            vy = glide;
        }
        break;
    }
    case 1: // Hover: no vertical drift
        vy = 0.0f;
        break;
    case 0: // Glide (MotionFly default vertical)
    default:
        vy = glide;
        break;
    }
    g_prevVy = vy;

    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return;
    vel->x = vx;
    vel->y = vy;
    vel->z = vz;
}

// ============================================================================
// ECSDump — one-shot discovery of the local player's ECS component layout.
//
// Walks every pool in the player's registry, resolves the player element the
// same way the game does, snapshots 32 floats per element, repeats 2 s later,
// and logs the DELTAS (rotating the view between the two dumps highlights the
// rotation component; taking damage highlights health, etc.). Auto-disables.
// ============================================================================

constexpr std::size_t kMaxPools = 160;
constexpr int kProbeFloats = 16;
constexpr std::uint64_t kDumpDelayTicks = 40; // ~2 s at 20 tps

struct DumpProbe {
    std::uint32_t hash{0};
    ecs::ElementInfo el{};
    float before[kProbeFloats]{};
    bool haveBefore{false};
};

std::vector<DumpProbe> g_dumpProbes;
long g_dumpStartTick = -1;
bool g_dumped = false;

void dumpSnapshot(bool second) {
    namespace E = eclient_runtime::ecs;
    for (auto& probe : g_dumpProbes) {
        float now[kProbeFloats]{};
        bool ok = false;
        // Probe multiple plausible strides — element layout differs per
        // component; floats changing between snapshots identify semantics.
        static constexpr std::uint64_t kStrides[] = {0x10, 0x18, 0x20, 0x30, 0x50};
        for (std::uint64_t stride : kStrides) {
            ok = E::readElementFloats(probe.el.block + probe.el.inBlock * stride, now, kProbeFloats);
            if (ok) break;
        }
        if (!ok) continue;

        if (!second) {
            std::memcpy(probe.before, now, sizeof(now));
            probe.haveBefore = true;
            continue;
        }
        if (!probe.haveBefore) continue;

        std::uint32_t beforeW[kProbeFloats];
        std::uint32_t nowW[kProbeFloats];
        std::memcpy(beforeW, probe.before, sizeof(beforeW));
        std::memcpy(nowW, now, sizeof(nowW));

        // Float delta → moving/rotating values (rotation comp, velocity...).
        bool floatChanged = false;
        // Raw word delta where floats barely moved → boolean/counter FLAGS
        // (spam jump+sneak during the window to expose input flag comps).
        bool flagChanged = false;
        for (int i = 0; i < kProbeFloats; ++i) {
            const float d = std::fabs(now[i] - probe.before[i]);
            if (d > 1.0e-4f) floatChanged = true;
            else if (nowW[i] != beforeW[i]) flagChanged = true;
        }
        if (!floatChanged && !flagChanged) continue;

        std::string line;
        char chunk[192];
        std::snprintf(chunk, sizeof(chunk),
                      "ECS-%s hash=0x%08x %s\n  before:",
                      floatChanged ? "DELTA" : "FLAG",
                      probe.hash, E::describe(probe.el).c_str());
        line += chunk;
        for (int i = 0; i < kProbeFloats; ++i) {
            std::snprintf(chunk, sizeof(chunk), " %.3g", probe.before[i]);
            line += chunk;
        }
        line += "\n  after :";
        for (int i = 0; i < kProbeFloats; ++i) {
            std::snprintf(chunk, sizeof(chunk), " %.3g", now[i]);
            line += chunk;
        }
        if (flagChanged) {
            line += "\n  words :";
            for (int i = 0; i < kProbeFloats; ++i) {
                std::snprintf(chunk, sizeof(chunk), " %08x->%08x", beforeW[i], nowW[i]);
                line += chunk;
            }
        }
        __android_log_print(ANDROID_LOG_INFO, TAG, "%s", line.c_str());
    }
}

void tickEcsDump(void* player) {
    if (g_dumped) return;
    if (g_dumpStartTick < 0) {
        g_dumpStartTick = g_tick;
        g_dumpProbes.clear();

        namespace E = eclient_runtime::ecs;
        const std::uint32_t eid = entityIdOf(player);
        const auto pools = E::listPools(reinterpret_cast<std::uintptr_t>(player));
        __android_log_print(ANDROID_LOG_INFO, TAG,
            "ECSDump: %zu pools, eid=0x%x — snapshot 1 now,"
            " snapshot 2 in ~2 s. During the window: ROTATE the view,"
            " SPAM jump+sneak, take a hit if possible.",
            pools.size(), eid);

        for (const auto& d : pools) {
            if (g_dumpProbes.size() >= kMaxPools) break;
            DumpProbe probe{};
            probe.hash = d.typeHash;
            probe.el = E::resolveElement(d.pool, eid);
            if (probe.el.valid) g_dumpProbes.push_back(probe);
        }
        __android_log_print(ANDROID_LOG_INFO, TAG, "ECSDump: %zu pools resolve for local player",
                            g_dumpProbes.size());
        dumpSnapshot(false);
        return;
    }

    if (g_tick - g_dumpStartTick >= static_cast<long>(kDumpDelayTicks)) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "ECSDump: snapshot sqrt-2 — deltas follow");
        dumpSnapshot(true);
        __android_log_print(ANDROID_LOG_INFO, TAG, "ECSDump: done (module auto-disabled)");
        g_dumped = true;
        mgr().setEnabled("ECSDump", false);
    }
}

void resetState() {
    g_hurtState.clear();
    g_attackTimer = 0;
    g_dumpProbes.clear();
    g_dumpStartTick = -1;
    g_dumped = false;
}

} // namespace

bool initialize(std::uintptr_t minecraftBase) {
    if (minecraftBase == 0) return false;
    base = minecraftBase;

    getRuntimeActors = reinterpret_cast<GetRuntimeActorFn>(fn(GetRuntimeActor));
    attack = reinterpret_cast<AttackFn>(fn(Attack));
    isAttackable = reinterpret_cast<IsAttackableFn>(fn(IsAttackable));
    getHealth = reinterpret_cast<GetHealthFn>(fn(GetHealth));
    getEntityTypeId = reinterpret_cast<GetEntityTypeIdFn>(fn(GetEntityTypeId));

    resetState();
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "Melody ports ready: KillAura2 + Fly(velocity) + ECSDump");
    return true;
}

void onNormalTick(void* localPlayer) {
    if (!localPlayer) return;
    ++g_tick;

    // Prune hurt-state map of stale pointers occasionally.
    if ((g_tick & 0x3FF) == 0 && g_hurtState.size() > 512) g_hurtState.clear();

    if (mgr().enabled("KillAura2")) tickKillAura2(localPlayer);
    if (mgr().enabled("Fly")) {
        tickFly(localPlayer);
    } else {
        g_haveLastFlyPos = false;
        g_jetSustainTicks = 0;
        g_prevVy = 0.0f;
    }
    if (mgr().enabled("ECSDump")) tickEcsDump(localPlayer);
}

} // namespace eclient_runtime::modules::melody
