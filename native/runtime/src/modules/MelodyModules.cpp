#include "modules/MelodyModules.h"

#include "bridge/DebugFiles.h"
#include "memory/ComponentWalk.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"

#include <android/log.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
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
// Fly — from scratch, device-built (v3). No momentum ratchet.
//
// Why the previous attempt felt possessed: it rescaled the CURRENT velocity
// every tick — but that velocity already contained what we wrote last tick,
// so one stick flick fed itself forever. v3 is input-observer-based:
//
//   Vanilla physics per tick: vel = FRICTION * vel_prev + inputAccel
//                                   (+ gravity −0.08 in y, + impulses)
//
//   We always know what WE wrote last tick (g_flyPrevOut). Subtracting it
//   (scaled by air friction 0.98) leaves the PURE vanilla input
//   acceleration — the stick — as a clean signal:
//
//   inputAccel ≈ vel_now − 0.98 * g_flyPrevOut
//
//   Consequences the user actually asked for:
//     * release stick → accel = 0 → we write 0 → dead stop + hover
//     * tilt a little → tiny accel → tiny proportional speed (move an inch)
//     * direction follows the stick instantly, no memory
//
//   Vertical:
//     * Hover  — vy = 0 always
//     * Glide  — vy = glide slider (deliberate slow descent to land)
//     * Jetpack— glide base; a REAL jump impulse (+0.3+ residue in y after
//                friction/gravity separation) sustains +UpSpeed for 10 ticks
//                (tap jump = climb; land = glide back down)
//   Hold-JUMP/hold-SNEAK verticals need the input flag components (ECSDump
//   discovery, same as rotation) — until then taps, not holds.
// ============================================================================

Vec3 g_flyPrevOut{};
Vec3 g_lastFlyPos{};
bool g_haveLastFlyPos = false;
int g_jetSustainTicks = 0;

void tickFly(void* player) {
    Vec3* vel = velocityPtr(player);
    if (!vel) return;

    const float rawX = vel->x, rawY = vel->y, rawZ = vel->z;
    if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawZ)) return;

    // Plausibility gate on ALL derived-layout writes (per port policy).
    if (std::fabs(rawX) > 100.0f || std::fabs(rawY) > 100.0f || std::fabs(rawZ) > 100.0f) return;

    // Anti-rubberband: skip a tick where the server teleported us so we never
    // fight position corrections (prev-write reset to avoid a stale residue).
    Vec3 pos{};
    if (!getPosition(player, pos)) return;
    if (g_haveLastFlyPos) {
        const float dx = pos.x - g_lastFlyPos.x;
        const float dy = pos.y - g_lastFlyPos.y;
        const float dz = pos.z - g_lastFlyPos.z;
        if ((dx * dx + dy * dy + dz * dz) > 4.5f * 4.5f) {
            g_lastFlyPos = pos;
            g_flyPrevOut = {0.0f, 0.0f, 0.0f};
            return;
        }
    }
    g_haveLastFlyPos = true;
    g_lastFlyPos = pos;

    const float horizBps = mgr().floatSetting("Fly", "HSpeed BPS", 12.0f);
    const float glide = mgr().floatSetting("Fly", "Glide", -0.02f);
    const float upBps = mgr().floatSetting("Fly", "UpSpeed BPS", 10.0f);
    const int verticalMode = mgr().enumSetting("Fly", "VerticalMode", 2);

    // ---- separate our own contribution (horizontal) ----
    constexpr float AIR_FRICTION = 0.98f;
    float ax = rawX - AIR_FRICTION * g_flyPrevOut.x;
    float az = rawZ - AIR_FRICTION * g_flyPrevOut.z;
    // Clamp to the physical accel bound (garbage/teleport/friction-mismatch
    // residues get clipped instead of steering us), then dead-zone.
    ax = std::clamp(ax, -0.35f, 0.35f);
    az = std::clamp(az, -0.35f, 0.35f);
    const float amag = std::sqrt(ax * ax + az * az);

    float outX = 0.0f, outZ = 0.0f;
    constexpr float DEADZONE = 0.004f;
    constexpr float FULL_STICK_ACCEL = 0.026f; // vanilla sprint air accel ≈ analog 1.0
    if (amag > DEADZONE) {
        const float target = std::min(horizBps / 20.0f, 2.4449f);
        const float ratio = std::min(amag / FULL_STICK_ACCEL, 1.0f);
        const float t = target * ratio;
        outX = ax / amag * t;
        outZ = az / amag * t;
    }
    // else: no stick input → write 0 → instant hover stop (the whole point).

    // ---- vertical ----
    float outY;
    switch (verticalMode) {
    case 2: { // Jetpack: glide base + impulse-sustained climb on real jumps
        // Separate vertical the same way: remove our prev write and gravity.
        const float vyResidue = rawY - AIR_FRICTION * g_flyPrevOut.y + 0.08f;
        if (vyResidue > 0.25f && vyResidue < 1.5f) {
            g_jetSustainTicks = 10; // a real jump impulse just happened
        }
        if (g_jetSustainTicks > 0) {
            outY = std::min(upBps / 20.0f, 2.4449f);
            --g_jetSustainTicks;
        } else {
            outY = glide;
        }
        break;
    }
    case 1: // Glide: deliberate descend-to-land at the slider value
        outY = glide;
        break;
    case 0: // Hover: dead pin
    default:
        outY = 0.0f;
        break;
    }

    if (!std::isfinite(outX) || !std::isfinite(outY) || !std::isfinite(outZ)) return;

    const int writeMode = mgr().enumSetting("Fly", "WriteMode", 0);
    if (writeMode == 1) {
        // Position mode: move the state-vector position (and prevPos by the
        // same delta so render interpolation stays clean), zero velocity.
        // Fallback for servers that overwrite client velocity every tick.
        if (auto* sv = static_cast<float*>(
                *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetPositionField))) {
            sv[0] += outX; sv[1] += outY; sv[2] += outZ; // pos     @+0x00
            sv[3] += outX; sv[4] += outY; sv[5] += outZ; // prevPos @+0x0C
        }
        vel->x = 0.0f; vel->y = 0.0f; vel->z = 0.0f;
        g_flyPrevOut = {0.0f, 0.0f, 0.0f};
        g_lastFlyPos = {pos.x + outX, pos.y + outY, pos.z + outZ};
        return;
    }

    vel->x = outX;
    vel->y = outY;
    vel->z = outZ;
    g_flyPrevOut = {outX, outY, outZ};
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
debugfiles::PublicLog g_ecsLog;

void ecsLogLine(const std::string& line) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", line.c_str());
    g_ecsLog.write(line.c_str());
}

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
        ecsLogLine(line);
    }
}

void tickEcsDump(void* player) {
    if (g_dumped) return;
    if (g_dumpStartTick < 0) {
        g_dumpStartTick = g_tick;
        g_dumpProbes.clear();
        g_ecsLog.open("ecsdump.txt");

        namespace E = eclient_runtime::ecs;
        const std::uint32_t eid = entityIdOf(player);
        const auto pools = E::listPools(reinterpret_cast<std::uintptr_t>(player));
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "ECSDump: %zu pools, eid=0x%x — snapshot 1 now,"
            " snapshot 2 in ~2 s. During the window: ROTATE the view,"
            " SPAM jump+sneak, take a hit if possible. file=%s",
            pools.size(), eid, g_ecsLog.where().c_str());
        ecsLogLine(buf);

        for (const auto& d : pools) {
            if (g_dumpProbes.size() >= kMaxPools) break;
            DumpProbe probe{};
            probe.hash = d.typeHash;
            probe.el = E::resolveElement(d.pool, eid);
            if (probe.el.valid) g_dumpProbes.push_back(probe);
        }
        std::snprintf(buf, sizeof(buf), "ECSDump: %zu pools resolve for local player",
                      g_dumpProbes.size());
        ecsLogLine(buf);
        dumpSnapshot(false);
        return;
    }

    if (g_tick - g_dumpStartTick >= static_cast<long>(kDumpDelayTicks)) {
        ecsLogLine("ECSDump: snapshot 2 — deltas follow");
        dumpSnapshot(true);
        std::string done = "ECSDump: done (module auto-disabled) file=" + g_ecsLog.where();
        ecsLogLine(done);
        g_ecsLog.close();
        g_dumped = true;
        mgr().setEnabled("ECSDump", false);
    }
}

// ============================================================================
// FlyDebug — per-tick telemetry to answer *why* the fly misbehaves.
// Writes 240 ticks (~12 s) of raw velocity in / fly write out / re-read
// verify / resulting position to logcat AND to
//   /sdcard/Android/data/com.mojang.minecraftpe/files/flydebug.txt
// so it can be pulled with any file manager. Self-disables when done;
// re-enable for another window.
// ============================================================================

constexpr long kFlyDebugTicks = 240;

long g_debugLeft = 0;
bool g_debugWasFly = false;
debugfiles::PublicLog g_flyLog;
Vec3 g_debugPreVel{}, g_debugPrePos{};
bool g_debugHavePre = false;

void flyDebugLine(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
    g_flyLog.write(buf);
}

void tickFlyDebugPre(void* player) {
    if (g_debugLeft <= 0) {
        g_debugLeft = kFlyDebugTicks;
        g_flyLog.open("flydebug.txt");
        flyDebugLine("FLD-BEGIN tick=%ld mode=%d write=%d h=%.1f glide=%.3f up=%.1f file=%s",
                     g_tick,
                     mgr().enumSetting("Fly", "VerticalMode", 2),
                     mgr().enumSetting("Fly", "WriteMode", 0),
                     mgr().floatSetting("Fly", "HSpeed BPS", 12.0f),
                     mgr().floatSetting("Fly", "Glide", -0.02f),
                     mgr().floatSetting("Fly", "UpSpeed BPS", 10.0f),
                     g_flyLog.where().c_str());
        g_debugWasFly = mgr().enabled("Fly");
    }
    Vec3* vel = velocityPtr(player);
    Vec3 pos{};
    getPosition(player, pos);
    if (vel) g_debugPreVel = *vel;
    g_debugPrePos = pos;
    g_debugHavePre = vel != nullptr;

    const bool flyNow = mgr().enabled("Fly");
    if (flyNow != g_debugWasFly) {
        flyDebugLine("FLD-TOGGLE fly=%s t=%ld", flyNow ? "ON" : "off", g_tick);
        g_debugWasFly = flyNow;
    }
}

void tickFlyDebugPost(void* player) {
    if (g_debugLeft <= 0) return;
    Vec3* vel = velocityPtr(player);
    Vec3 pos{};
    getPosition(player, pos);
    void* sv = vel ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetPositionField)
                   : nullptr;
    flyDebugLine("FLD t=%ld pre=(%.3f,%.3f,%.3f) out=(%.3f,%.3f,%.3f) post=(%.3f,%.3f,%.3f) pos=(%.2f,%.2f,%.2f) sv=%p",
                 g_tick,
                 g_debugPreVel.x, g_debugPreVel.y, g_debugPreVel.z,
                 g_flyPrevOut.x, g_flyPrevOut.y, g_flyPrevOut.z,
                 vel ? vel->x : 0.f, vel ? vel->y : 0.f, vel ? vel->z : 0.f,
                 pos.x, pos.y, pos.z, sv);
    if (--g_debugLeft <= 0) {
        flyDebugLine("FLD-END file=%s — send it (logcat tag EClientMelody)", g_flyLog.where().c_str());
        g_flyLog.close();
        mgr().setEnabled("FlyDebug", false);
    }
}

void resetState() {
    g_hurtState.clear();
    g_attackTimer = 0;
    g_dumpProbes.clear();
    g_dumpStartTick = -1;
    g_dumped = false;
    g_debugLeft = 0;
    g_debugHavePre = false;
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

    const bool debug = mgr().enabled("FlyDebug");
    if (debug) tickFlyDebugPre(localPlayer);
    if (mgr().enabled("KillAura2")) tickKillAura2(localPlayer);
    if (mgr().enabled("Fly")) {
        tickFly(localPlayer);
    } else {
        g_haveLastFlyPos = false;
        g_jetSustainTicks = 0;
        g_flyPrevOut = {0.0f, 0.0f, 0.0f};
    }
    if (debug) tickFlyDebugPost(localPlayer);
    if (mgr().enabled("ECSDump")) tickEcsDump(localPlayer);
}

} // namespace eclient_runtime::modules::melody
