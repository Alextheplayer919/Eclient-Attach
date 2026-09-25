#include "modules/MelodyModules.h"

#include "bridge/DebugFiles.h"
#include "memory/ComponentWalk.h"
#include "memory/NativeMemory.h"
#include "modules/FlyInput.h"
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

// ---------------------------------------------------------------------------
// World-stability gate.
//
// During world/server JOIN the LocalPlayer exists but its state vector, ECS
// registry and derived structures are still being torn down/rebuilt. Any
// deref in that window (fly debug, ECS walk, aura scans) segfaults the game.
//
// heavyPlayerSafe() performs only GUARDED reads (NativeMemory::read validates
// against /proc/self/maps before copying) and plausibility-checks the state
// vector. The result is latched: while latched-open we do cheap checks only;
// a huge position jump (teleport / dimension change / join) force-closes the
// latch so everything re-validates instead of crashing on the half-built data.
// ---------------------------------------------------------------------------

bool g_stableLatch = false;
long g_lastStableCheck = -1000;
long g_tick = 0; // declared early: the world-stability gate reads it

bool heavyPlayerSafe(void* player) {
    using memory::NativeMemory;
    auto& mem = NativeMemory::instance();
    std::uintptr_t sv = 0;
    if (!mem.read(reinterpret_cast<std::uintptr_t>(player) + GetPositionField, &sv, sizeof(sv)) || !sv)
        return false;
    float buf[6]{}; // pos + prevPos
    if (!mem.read(sv, buf, sizeof(buf))) return false;
    for (float v : buf) if (!std::isfinite(v)) return false;
    if (std::fabs(buf[0]) > 1.0e7f || std::fabs(buf[2]) > 1.0e7f || std::fabs(buf[1]) > 1.0e5f)
        return false;
    return true;
}

// Called once per normalTick before any module code. Returns true when module
// logic may touch player memory; false = this tick is a join window (skip).
bool worldReady(void* player) {
    if (!player) { g_stableLatch = false; return false; }
    if (g_stableLatch) {
        if (g_tick - g_lastStableCheck >= 40) {
            g_lastStableCheck = g_tick;
            g_stableLatch = heavyPlayerSafe(player);
            return g_stableLatch;
        }
        // Cheap hot-path check: null gates + finite values after plain deref.
        void* sv = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetPositionField);
        if (!sv) { g_stableLatch = false; return false; }
        const float* p = static_cast<const float*>(sv);
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
            g_stableLatch = false;
            return false;
        }
        return true;
    }
    g_stableLatch = heavyPlayerSafe(player);
    g_lastStableCheck = g_tick;
    return g_stableLatch;
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
// Fly — E-Client's OWN fly, built from scratch for touch (no module ports).
//
// Why this works on mobile where ported keyboard flies never did:
//   * We own the ENTIRE touch pipeline (view-layer listener → JNI → GUI), so
//     instead of inventing keyboard surrogates (jetpack impulse residue, etc.)
//     we render REAL ▲/▼ hold buttons and read them like hardware keys:
//         hold ▲  → velocity.y = +V-Speed   (ascend)
//         hold ▼  → velocity.y = −V-Speed   (descend)
//         neither → velocity.y =  0.0f      (TRUE hover, pinned exactly —
//             no glide creep means there is nothing for an anticheat to
//             periodically correct, so no lagback stutter)
//   * Button touches are CONSUMED at our injection point before the game sees
//     them (gesture-latched per pointer id), so holding ▲ never spins the
//     camera; multi-pointer tracking means a second thumb can steer the
//     joystick / look around while one finger holds ▲.
//   * Horizontal: no key readback exists on touch, so direction comes from
//     measuring the vanilla input acceleration residue
//     (vel_now − AIR_FRICTION·our_prev_write), normalized and projected at
//     H-Speed — the joystick IS vanilla keyboard-equivalent input, so this
//     reads the same move vector the game itself produces.
//   * Anti-rubberband skip (server teleport >4.5 blk) + world-stability gate
//     stay as crash insurance; WriteMode=Position is the server-fallback.
// ============================================================================

Vec3 g_flyPrevOut{};
Vec3 g_lastFlyPos{};
bool g_haveLastFlyPos = false;

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
            g_stableLatch = false; // teleport/join → re-validate next tick
            return;
        }
    }
    g_haveLastFlyPos = true;
    g_lastFlyPos = pos;

    // Settings (registered in ModuleManager).
    const float hSpeed = mgr().floatSetting("Fly", "H-Speed", 1.0f);
    const float vSpeed = mgr().floatSetting("Fly", "V-Speed", 0.5f);

    // Horizontal: subtract what WE wrote last tick (scaled by air friction)
    // from what the game has now; the residue is pure vanilla input
    // acceleration — joystick direction on touch screens.
    constexpr float AIR_FRICTION = 0.98f;
    float ax = rawX - AIR_FRICTION * g_flyPrevOut.x;
    float az = rawZ - AIR_FRICTION * g_flyPrevOut.z;
    ax = std::clamp(ax, -0.35f, 0.35f); // physical accel bound, kills garbage
    az = std::clamp(az, -0.35f, 0.35f);
    const float amag = std::sqrt(ax * ax + az * az);

    // Vertical — E-Client's own fly: hold ▲ = ascend, hold ▼ = descend,
    // neither = true hover pinned at exactly 0 (no glide creep ⇒ nothing for
    // an anticheat to periodically correct ⇒ no lagback stutter).
    const bool upHeld = flyinput::heldUp();
    const bool downHeld = flyinput::heldDown();
    float outY = 0.0f;
    if (upHeld && !downHeld) outY = vSpeed;
    else if (downHeld && !upHeld) outY = -vSpeed;

    float outX = 0.0f, outZ = 0.0f;
    constexpr float DEADZONE = 0.004f;
    if (amag > DEADZONE) {
        outX = ax / amag * hSpeed;
        outZ = az / amag * hSpeed;
    }

    if (!std::isfinite(outX) || !std::isfinite(outY) || !std::isfinite(outZ)) return;

    const int writeMode = mgr().enumSetting("Fly", "WriteMode", 0);
    if (writeMode == 1) {
        // Position mode: move the state-vector position (and prevPos by the
        // same delta so render interpolation stays clean), zero velocity.
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

    // source: localPlayer->lerpMotion(moveVec);
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
        flyDebugLine("FLD-BEGIN tick=%ld h=%.2f v=%.2f buttons=%d write=%d up=%d down=%d file=%s",
                     g_tick,
                     mgr().floatSetting("Fly", "H-Speed", 1.0f),
                     mgr().floatSetting("Fly", "V-Speed", 0.5f),
                     mgr().boolSetting("Fly", "FlyButtons", true) ? 1 : 0,
                     mgr().enumSetting("Fly", "WriteMode", 0),
                     flyinput::heldUp() ? 1 : 0,
                     flyinput::heldDown() ? 1 : 0,
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

    // Stability gate: skip ALL memory-touching modules while the world/server
    // join rebuilds the actor structures (this was the join-crash root cause).
    if (!worldReady(localPlayer)) {
        // Let a disabled ECSDump re-arm so the next enable starts fresh.
        if (!mgr().enabled("ECSDump")) { g_dumped = false; g_dumpStartTick = -1; }
        g_haveLastFlyPos = false;
        flyinput::reset();
        g_flyPrevOut = {0.0f, 0.0f, 0.0f};
        return;
    }

    // Prune hurt-state map of stale pointers occasionally.
    if ((g_tick & 0x3FF) == 0 && g_hurtState.size() > 512) g_hurtState.clear();

    const bool debug = mgr().enabled("FlyDebug");
    if (debug) tickFlyDebugPre(localPlayer);
    if (mgr().enabled("KillAura2")) tickKillAura2(localPlayer);
    if (mgr().enabled("Fly")) {
        tickFly(localPlayer);
    } else {
        g_haveLastFlyPos = false;
        flyinput::reset();
        g_flyPrevOut = {0.0f, 0.0f, 0.0f};
    }
    if (debug) tickFlyDebugPost(localPlayer);
    if (mgr().enabled("ECSDump")) tickEcsDump(localPlayer);
}

} // namespace eclient_runtime::modules::melody
