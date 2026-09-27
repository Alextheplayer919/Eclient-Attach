#include "modules/MelodyModules.h"

#include "bridge/DebugFiles.h"
#include "kotlin/KotlinBridge.h"
#include "memory/ComponentWalk.h"
#include "memory/NativeMemory.h"
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
#include <limits>
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

#include "bridge/AgentState.h"

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

// Melody TargetUtils::isTargetValid hardcoded "not attackable" entity ids.
constexpr std::uint32_t kInvalidTypeIds[] = {
    0, 64, 66, 69, 70, 95, 4194372, 4194390, 4194391, 4194405, 12582992
};

bool typeIdBlocked(std::uint32_t id) {
    for (std::uint32_t blocked : kInvalidTypeIds) if (blocked == id) return true;
    return false;
}

// ── Actor identity (r29) ──────────────────────────────────────────────────
// The type id is a 4-byte ECS component (ActorTypeComponent, hash 0x4f6ba419,
// stride 4 — all disassembly-proven, see profile/Offsets_1_21_111.h). Reading
// it through our own guarded walk removes any dependence on the game's
// function ABI, and both KillAura2 and the bot use this one path.

struct TypeRead {
    std::uint32_t id{0};
    bool ok{false};           // a value was obtained (either path)
    bool viaComponent{false}; // ...from the ECS component, not the function
};

std::uintptr_t findPoolByHash(std::uintptr_t actor, std::uint32_t hash) {
    if (!actor || !hash) return 0;
    for (const auto& d : ecs::listPools(actor)) if (d.typeHash == hash) return d.pool;
    return 0;
}



TypeRead readActorTypeId(void* actor, std::uintptr_t typePool) {
    TypeRead r{};
    if (!actor) return r;
    if (typePool) {
        const ecs::ElementInfo el = ecs::resolveElement(typePool, entityIdOf(actor));
        if (el.valid) {
            std::uint32_t v = 0;
            if (memory::NativeMemory::instance().read(
                    el.block + el.inBlock * ActorTypeComponentStride, &v, sizeof(v))) {
                r.id = v; r.ok = true; r.viaComponent = true;
                return r;
            }
        }
    }
    if (getEntityTypeId) { r.id = getEntityTypeId(actor); r.ok = true; }
    return r;
}

// Equal *player* types. Masked on purpose: see TypeIdMask in the offsets header.
bool samePlayerType(std::uint32_t a, std::uint32_t b) {
    return a != 0 && b != 0 && ((a & TypeIdMask) == (b & TypeIdMask));
}

// Why an actor was rejected. The bot counts these, so "the bot sees nobody" can
// never again be a mystery: the funnel says which stage emptied the list.
enum class Reject : int {
    Ok = 0, NullOrSelf, NotAttackable, Dead, TypeBlocked, TypeMismatch, NoPosition, OutOfRange
};

Reject targetReject(void* player, void* actor, const Vec3& myPos, float range,
                    bool playersOnly, std::uint32_t myTypeId, std::uintptr_t typePool,
                    std::uint32_t* outTypeId) {
    if (outTypeId) *outTypeId = 0;
    if (!actor || actor == player) return Reject::NullOrSelf;
    if (!isAttackable || !isAttackable(actor)) return Reject::NotAttackable;
    if (!getHealth) return Reject::Dead;
    if (getHealth(actor) <= 0) return Reject::Dead;
    if (myTypeId != 0 || playersOnly) {
        const TypeRead t = readActorTypeId(actor, typePool);
        if (outTypeId) *outTypeId = t.id;
        if (t.ok) {
            if (typeIdBlocked(t.id)) return Reject::TypeBlocked;
            if (playersOnly && myTypeId != 0 && t.id != 0 &&
                !samePlayerType(t.id, myTypeId)) return Reject::TypeMismatch;
        }
    }
    Vec3 pos{};
    if (!getPosition(actor, pos)) return Reject::NoPosition;
    const float dx = pos.x - myPos.x, dy = pos.y - myPos.y, dz = pos.z - myPos.z;
    if ((dx * dx + dy * dy + dz * dz) > range * range) return Reject::OutOfRange;
    return Reject::Ok;
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

// ── Type-pool cache (r38) ───────────────────────────────────────────────────
// Walking the registry to find one pool costs one guarded read per field of
// every descriptor (~620 pools x 2 reads) PLUS a vector allocation, and botScan
// did it on every scan (twice a tick once KillAura2 and the bot both ran).
// The answer only changes when the WORLD changes, so it is cached against the
// local-player pointer and a tick budget; a world join replaces the pointer.
constexpr long kTypePoolCacheTicks = 200;   // ~10 s of safety margin
void* g_typePoolOwner = nullptr;
std::uintptr_t g_typePoolValue = 0;
long g_typePoolTick = -1;

std::uintptr_t typePoolForPlayer(void* player) {
    if (!player) return 0;
    if (g_typePoolOwner == player && g_typePoolValue &&
        g_tick - g_typePoolTick < kTypePoolCacheTicks) {
        return g_typePoolValue;
    }
    const std::uintptr_t pool = findPoolByHash(reinterpret_cast<std::uintptr_t>(player),
                                               ActorTypeComponentHash);
    if (pool) {
        g_typePoolOwner = player;
        g_typePoolValue = pool;
        g_typePoolTick = g_tick;
    } else {
        g_typePoolOwner = nullptr;
        g_typePoolValue = 0;
    }
    return pool;
}

void dropTypePoolCache() {
    g_typePoolOwner = nullptr;
    g_typePoolValue = 0;
    g_typePoolTick = -1;
}

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
    // One shared filter for KillAura2 and the bot (0 = resolve the type pool via
    // the function path; the bot passes a pool it resolved once per scan).
    return targetReject(player, actor, myPos, range, playersOnly, myTypeId, 0, nullptr) ==
           Reject::Ok;
}

// The rotation element may only be touched when the pool is known AND the
// element layout is pinned. The hash alone is NOT enough: the write below
// indexes block + inBlock*stride + yawOffset, so a non-zero hash with a zero
// stride would write into the component block itself.
bool rotationWritable() {
    return ActorRotationComponentHash != 0 && ActorRotationStride != 0 &&
           ActorRotationYawOffset != 0;
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

    // FPS: full entity enumeration (virtual calls over every actor) runs
    // every 2nd tick; the sorted target list is reused in between. At 20 tps
    // that is a 10 Hz target refresh — faster than any legit delay setting.
    static std::vector<std::pair<float, void*>> targets;
    static int scanDivider = 0;
    if ((scanDivider++ & 0x1) == 0) {
        targets.clear();
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
    }

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
        if (rotationMode != 0 && rotationWritable()) {
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

// Human name for a pool hash we have derived offline. Purely a label: it makes
// the on-device dump readable and turns "which of the 160 pools is rotation?"
// into a lookup. Unknown hashes print as-is; that is fine.
const char* componentName(std::uint32_t hash) {
    switch (hash) {
        case ActorRotationComponentHash: return "ActorRotationComponent";
        case ActorHeadRotationComponentHash: return "ActorHeadRotationComponent";
        case MobHurtTimeComponentHash: return "MobHurtTimeComponent";
        case MoveInputComponentHash: return "MoveInputComponent";
        case RawMoveInputComponentHash: return "RawMoveInputComponent";
        case HitboxComponentHash: return "HitboxComponent";
        default: return nullptr;
    }
}

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
        const char* known = componentName(probe.hash);
        std::snprintf(chunk, sizeof(chunk),
                      "ECS-%s hash=0x%08x%s%s %s\n  before:",
                      floatChanged ? "DELTA" : "FLAG",
                      probe.hash, known ? " " : "", known ? known : "",
                      E::describe(probe.el).c_str());
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


void resetState() {
    dropTypePoolCache();
    g_hurtState.clear();
    g_attackTimer = 0;
    g_dumpProbes.clear();
    g_dumpStartTick = -1;
    g_dumped = false;
}

} // namespace

// ============================================================================
// Bot support (r27) — see MelodyModules.h for the contract.
//
// Everything here is a thin, guarded query over the SAME helpers the PvP
// modules use. Nothing in this file decides anything: the Kotlin PvpBot owns
// the strategy, the scoring and the exploration.
// ============================================================================
namespace {

// The bot keeps its OWN health history rather than reusing g_hurtState: that
// map feeds KillAura2's Hurttime filter, and mixing "who damaged whom" into it
// would change that module's behaviour from the outside.
struct BotHealth { int health; long hurtUntil; };
std::unordered_map<void*, BotHealth> g_botHealth;

// Funnel counters for the last scan (r29). Everything the bot's target search
// can reject an actor for is counted, so an empty list always has a reason.
struct ScanStats {
    int actors{0};
    int attackable{0};
    int inRange{0};
    int picked{0};
    int rejectedNotAttackable{0};
    int rejectedDead{0};
    int rejectedTypeBlocked{0};
    int rejectedTypeMismatch{0};
    int rejectedNoPosition{0};
    int rejectedOutOfRange{0};
    std::uint32_t myTypeId{0};
    bool myTypeOk{false};
    bool myTypeViaComponent{false};
    bool fallback{false};
    int mode{0};
    int sampleCount{0};
    std::string ids;
};
std::vector<void*> g_botActors;          // last botScan order (index space)
std::vector<long> g_botSelfDamageTicks; // ticks at which WE took damage
int g_botSelfLastHealth = -1;

bool botRecentlyHurt(void* actor, int health) {
    const auto it = g_botHealth.find(actor);
    if (it == g_botHealth.end()) {
        g_botHealth.emplace(actor, BotHealth{health, 0});
        return false;
    }
    BotHealth& st = it->second;
    const bool dropped = health < st.health;
    if (dropped) st.hurtUntil = g_tick + 10; // 0.5 s vanilla invulnerability
    st.health = health;
    return g_tick < st.hurtUntil;
}

} // namespace

ScanStats g_scanStats;

std::string botScanDiag() {
    const ScanStats& s = g_scanStats;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "scan(mode=%d): actors=%d atk=%d dead=%d blocked=%d mismatch=%d noPos=%d "
        "outOfRange=%d inRange=%d picked=%d | myType=0x%x(%s) ids=[%s] fallback=%s",
        s.mode, s.actors, s.attackable, s.rejectedDead, s.rejectedTypeBlocked,
        s.rejectedTypeMismatch, s.rejectedNoPosition, s.rejectedOutOfRange,
        s.inRange, s.picked,
        s.myTypeId, s.myTypeOk ? (s.myTypeViaComponent ? "comp" : "fn") : "unknown",
        s.ids.c_str(), s.fallback ? "YES" : "no");
    return std::string(buf);
}

int botScan(void* player, float maxRange, int mode, BotActor* out, int cap) {
    g_botActors.clear();
    g_scanStats = ScanStats{};
    g_scanStats.mode = mode;
    if (!player || !out || cap <= 0) return -1;
    if (!getRuntimeActors || !isAttackable || !getHealth) return -1;

    void* level = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetLevelField);
    Vec3 myPos{};
    if (!level || !getPosition(player, myPos)) return -1;

    const float range = std::clamp(maxRange, 0.5f, 32.0f);

    // Resolve the type pool ONCE (all actors share the world's registry) and
    // read our own type id the same way we will read everyone else's.
    const std::uintptr_t typePool = typePoolForPlayer(player);
    const TypeRead mine = readActorTypeId(player, typePool);
    const std::uint32_t myTypeId = mine.id;
    g_scanStats.myTypeId = mine.id;
    g_scanStats.myTypeOk = mine.ok;
    g_scanStats.myTypeViaComponent = mine.viaComponent;

    std::vector<std::pair<float, void*>> found;

    auto run = [&](bool playersOnly) {
        found.clear();
        g_scanStats.actors = 0;
        g_scanStats.attackable = 0;
        g_scanStats.rejectedDead = 0;
        g_scanStats.rejectedTypeBlocked = 0;
        g_scanStats.rejectedTypeMismatch = 0;
        g_scanStats.rejectedNoPosition = 0;
        g_scanStats.rejectedOutOfRange = 0;
        g_scanStats.inRange = 0;

        for (void* actor : getRuntimeActors(level)) {
            if (actor == player) continue;
            ++g_scanStats.actors;
            std::uint32_t seenId = 0;
            const Reject why = targetReject(player, actor, myPos, range, playersOnly,
                                            myTypeId, typePool, &seenId);
            if (why == Reject::NullOrSelf) { --g_scanStats.actors; continue; }
            (void)seenId; // ids are sampled from the surviving actors below
            switch (why) {
                case Reject::NotAttackable:
                    g_scanStats.rejectedNotAttackable++; continue;
                case Reject::Dead:
                    g_scanStats.rejectedDead++; continue;
                case Reject::TypeBlocked:
                    g_scanStats.rejectedTypeBlocked++; continue;
                case Reject::TypeMismatch:
                    g_scanStats.rejectedTypeMismatch++; continue;
                case Reject::NoPosition:
                    g_scanStats.rejectedNoPosition++; continue;
                case Reject::OutOfRange:
                    g_scanStats.rejectedOutOfRange++; continue;
                default: break;
            }
            ++g_scanStats.attackable;
            ++g_scanStats.inRange;
            Vec3 pos{};
            if (!getPosition(actor, pos)) continue;
            const float dx = pos.x - myPos.x, dy = pos.y - myPos.y, dz = pos.z - myPos.z;
            found.emplace_back(dx * dx + dy * dy + dz * dz, actor);
        }
        std::sort(found.begin(), found.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    };

    const bool wantPlayers = (mode != 2);
    run(wantPlayers);

    // Fallback (mode 1): the player filter emptied the list while other
    // attackable actors were in range. Fight anyway, and say so.
    if (mode == 1 && found.empty() && g_scanStats.rejectedTypeMismatch > 0) {
        run(false);
        g_scanStats.fallback = true;
    }

    int n = 0;
    for (const auto& [d2, actor] : found) {
        if (n >= cap) break;
        Vec3 pos{}, vel{};
        if (!getPosition(actor, pos)) continue;
        if (Vec3* v = velocityPtr(actor)) {
            if (std::isfinite(v->x) && std::isfinite(v->y) && std::isfinite(v->z)) vel = *v;
        }
        const int hp = getHealth(actor);
        const bool hurt = botRecentlyHurt(actor, hp);
        const TypeRead t = readActorTypeId(actor, typePool);
        if (t.ok && g_scanStats.sampleCount < 4) {
            char idbuf[24];
            std::snprintf(idbuf, sizeof(idbuf), "0x%x", t.id);
            if (g_scanStats.ids.find(idbuf) == std::string::npos) {
                if (!g_scanStats.ids.empty()) g_scanStats.ids += ",";
                g_scanStats.ids += idbuf;
                ++g_scanStats.sampleCount;
            }
        }
        out[n] = BotActor{pos.x, pos.y, pos.z, vel.x, vel.y, vel.z,
                          std::sqrt(d2), hp, t.id,
                          entityIdOf(actor),
                          (hurt ? kBotFlagHurt : 0) | kBotFlagAttackable};
        g_botActors.push_back(actor);
        ++n;
    }
    g_scanStats.picked = n;
    return n;
}

bool botAttackIndex(void* player, int index) {
    if (!player || !attack) return false;
    if (index < 0 || static_cast<std::size_t>(index) >= g_botActors.size()) return false;
    const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(player);
    void* gameMode = *reinterpret_cast<void**>(p + GetGameModeField);
    if (!gameMode) return false;
    attack(gameMode, g_botActors[static_cast<std::size_t>(index)], true);
    return true;
}

bool botAimAt(void* player, int index, float* outYawPitch) {
    if (!player || !outYawPitch) return false;
    if (index < 0 || static_cast<std::size_t>(index) >= g_botActors.size()) return false;
    Vec3 myPos{}, targetPos{};
    if (!getPosition(player, myPos)) return false;
    if (!getPosition(g_botActors[static_cast<std::size_t>(index)], targetPos)) return false;
    const Rot rot = getRotations(myPos, targetPos);
    if (!std::isfinite(rot.yaw) || !std::isfinite(rot.pitch)) return false;
    outYawPitch[0] = rot.yaw;
    outYawPitch[1] = rot.pitch;
    return true;
}

bool botSelf(void* player, float* outSelf) {
    if (!player || !outSelf || !getHealth) return false;
    Vec3 pos{}, vel{};
    if (!getPosition(player, pos)) return false;
    if (Vec3* v = velocityPtr(player)) {
        if (std::isfinite(v->x) && std::isfinite(v->y) && std::isfinite(v->z)) vel = *v;
    }

    const int hp = getHealth(player);
    bool damagedNow = false;
    if (g_botSelfLastHealth >= 0 && hp < g_botSelfLastHealth) {
        damagedNow = true;
        g_botSelfDamageTicks.push_back(g_tick);
        if (g_botSelfDamageTicks.size() > 64) g_botSelfDamageTicks.erase(g_botSelfDamageTicks.begin());
    }
    g_botSelfLastHealth = hp;

    // 5 s window (100 ticks at 20 tps) — "am I getting hit non stop right now?"
    std::size_t recent = 0;
    for (long t : g_botSelfDamageTicks) if (g_tick - t <= 100) ++recent;
    const long sinceLast = g_botSelfDamageTicks.empty() ? -1 : (g_tick - g_botSelfDamageTicks.back());

    outSelf[0] = pos.x; outSelf[1] = pos.y; outSelf[2] = pos.z;
    outSelf[3] = vel.x; outSelf[4] = vel.y; outSelf[5] = vel.z;
    outSelf[6] = static_cast<float>(hp);
    outSelf[7] = (damagedNow || (sinceLast >= 0 && sinceLast <= 10)) ? 1.0f : 0.0f;
    outSelf[8] = static_cast<float>(recent);
    outSelf[9] = static_cast<float>(sinceLast);
    return true;
}

bool botRotationAvailable() { return rotationWritable(); }

// ── Loopback-agent seam (r36) ───────────────────────────────────────────────
// These answer questions the app asks through the bridge. They are deliberately
// thin: every one of them delegates to the same validated path the modules use,
// so the app can never be told something the module layer would disagree with.

std::uint32_t botMyTypeId(bool* ok) {
    if (ok) *ok = g_scanStats.myTypeOk;
    return g_scanStats.myTypeId;
}

bool botIsPlayerType(std::uint32_t typeId) {
    // No own type resolved => no opinion. Reporting "player" here would let the
    // app aim at whatever happens to be nearby; reporting "not a player" would
    // hide real players. Both are worse than saying nothing.
    if (!g_scanStats.myTypeOk) return false;
    return samePlayerType(typeId, g_scanStats.myTypeId);
}

bool botApplyRotation(void* player, float yaw, float pitch) {
    if (!player) return false;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return false;
    // Same guard as rotationWritable(): hash AND stride AND yaw offset. Until the
    // ActorRotationComponent element layout is derived, all three fail, so this
    // returns false — which is the truthful answer, not a placeholder. The write
    // body lands in P3 with the constants, behind this same guard.
    if (!rotationWritable()) return false;
    return false;   // unreachable today; P3 replaces this line
}

bool botApplyInput(void* player, float forward, float strafe, bool jump, bool sneak) {
    (void)jump; (void)sneak;
    if (!player) return false;
    if (!std::isfinite(forward) || !std::isfinite(strafe)) return false;
    if (forward < -1.0f || forward > 1.0f || strafe < -1.0f || strafe > 1.0f) return false;
    // The MoveInputComponent element layout is the gate — see DO_NOT_BREAK.md:
    // movement is input-based walking, and an input write without the derived
    // layout is exactly the kind of guess that used to land in the wrong field.
    if (MoveInputElementStride == 0 || MoveInputVecOffset == 0) return false;
    return false;   // unreachable today; P3 replaces this line
}

// The local player's rotation element address, or 0 when unavailable. Same
// guarded resolution the rotation write uses (pool by hash, element by entity
// id) — one path, so a wrong hash shows up identically in both.
std::uintptr_t rotationElement(void* player) {
    if (!player || !rotationWritable()) return 0;
    const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(player);
    std::uintptr_t pool = 0;
    for (const auto& d : ecs::listPools(p)) {
        if (d.typeHash == ActorRotationComponentHash) { pool = d.pool; break; }
    }
    if (!pool) return 0;
    const ecs::ElementInfo el = ecs::resolveElement(pool, entityIdOf(player));
    if (!el.valid) return 0;
    return el.block + el.inBlock * ActorRotationStride + ActorRotationYawOffset;
}

// Yaw in degrees, or NaN when it cannot be read. Reading is as constrained as
// writing: without the stride and the yaw offset we would not know where the
// field is, and a wrong read would silently steer the bot somewhere else.
float botPlayerYaw(void* player) {
    const std::uintptr_t el = rotationElement(player);
    if (!el) return std::numeric_limits<float>::quiet_NaN();
    float yaw = 0.0f;
    if (!memory::NativeMemory::instance().read(el, &yaw, sizeof(yaw))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (!std::isfinite(yaw) || std::fabs(yaw) > 3600.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return yaw;
}

bool initialize(std::uintptr_t minecraftBase) {
    if (minecraftBase == 0) return false;
    base = minecraftBase;

    getRuntimeActors = reinterpret_cast<GetRuntimeActorFn>(fn(GetRuntimeActor));
    attack = reinterpret_cast<AttackFn>(fn(Attack));
    isAttackable = reinterpret_cast<IsAttackableFn>(fn(IsAttackable));
    getHealth = reinterpret_cast<GetHealthFn>(fn(GetHealth));
    getEntityTypeId = reinterpret_cast<GetEntityTypeIdFn>(fn(GetEntityTypeId));

    resetState();
    // Bot data plane (r27): a fresh bind means a fresh world — drop every
    // stale actor pointer and health baseline instead of comparing against
    // a previous session's numbers.
    g_botHealth.clear();
    g_botActors.clear();
    g_botSelfDamageTicks.clear();
    g_botSelfLastHealth = -1;
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "PvP modules ready: KillAura2 + ECSDump (Fly/FlyDebug removed in r25)");
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
        // Not in a world: nothing the pool cache says is trustworthy next time.
        dropTypePoolCache();
        return;
    }

    // Prune hurt-state map of stale pointers occasionally.
    if ((g_tick & 0x3FF) == 0 && g_hurtState.size() > 512) g_hurtState.clear();

    // Loopback agent (r36): latch this tick's world state for the companion app,
    // then execute whatever intents it queued. publish() runs FIRST so the actor
    // order the app receives is the same order the attacks below index into.
    // Both are no-ops unless a client is connected.
    agentstate::publish(localPlayer);
    agentstate::executeQueued(localPlayer);

    if (mgr().enabled("KillAura2")) tickKillAura2(localPlayer);

    // Kotlin-language modules (Hitbox / AutoTotem today, the PvP bot next): one
    // JNI dispatch per tick. Shares this tick's world-stability gate, so Kotlin
    // modules never touch memory while the world is still rebuilding.
    if (kotlinbridge::anyKotlinModuleEnabled()) kotlinbridge::dispatchTick(localPlayer);

    if (mgr().enabled("ECSDump")) tickEcsDump(localPlayer);
}

} // namespace eclient_runtime::modules::melody
