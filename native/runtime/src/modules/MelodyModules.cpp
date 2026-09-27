#include "modules/MelodyModules.h"

#include <atomic>

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

// ── Ability / rotation / movement API (r40) ────────────────────────────────
// These are the game's OWN accessors, resolved from the Apollon-validated
// table. Flying through setAbility is what the engine expects; it is why this
// works where writing the state vector's velocity never did (the engine
// overwrote it every tick, and the server never agreed we were flying).
using GetAbilitiesFn = void* (*)(void* player);          // -> Abilities*
using GetLayerFn = void* (*)(void* abilities, int index); // -> LayeredAbilities*
using SetAbilityFn = void (*)(void* layer, int index, bool on);
using SetClimbingFn = void (*)(void* actor, bool);
using SetYHeadRotFn = void (*)(void* actor, float yaw);
using GetYHeadRotFn = float (*)(void* actor);
using SetYBodyRotFn = void (*)(void* mob, float yaw);

GetAbilitiesFn getAbilities = nullptr;
GetLayerFn getLayer = nullptr;
SetAbilityFn setAbility = nullptr;
SetClimbingFn setClimbing = nullptr;
SetYHeadRotFn setYHeadRot = nullptr;
GetYHeadRotFn getYHeadRot = nullptr;
SetYBodyRotFn setYBodyRot = nullptr;

// Rotation readiness is probed on the GAME thread and published as an atomic,
// because the bridge thread also asks for it (capabilities) and must never
// dereference game memory itself.
std::atomic<bool> g_rotationReady{false};

// Fly latches what it turned on so it can turn exactly that off again.
bool g_flyEngaged = false;

std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }

// Resolve an API function pointer, treating offset 0 as "not derived yet".
// Calling fn(0) hands back the module base and the first call jumps into an
// arbitrary instruction, so this is the difference between a module that fails
// inert and a module that crashes the game on enable.
template <typename Fn>
Fn apiPtr(std::uint64_t offset) {
    return offset ? reinterpret_cast<Fn>(fn(offset)) : nullptr;
}

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

// KillAura2's own funnel. The bot has had one since r29 (g_scanStats, filled
// by botScan); the module people actually use did not, so "killaura does
// nothing" had no way to say WHICH stage emptied its target list. Same shape as
// ScanStats, same caveat: written on the game thread, read by the panel.
struct AuraStats {
    int actors{0}, ok{0}, notAttackable{0}, dead{0}, typeBlocked{0},
        typeMismatch{0}, noPosition{0}, outOfRange{0}, hurtFiltered{0};
    bool scanned{false};
    bool playersOnly{false};
    int rangeX10{0};
};
AuraStats g_auraStats;
std::atomic<long> g_auraAttacks{0};
std::atomic<long> g_auraScanTick{-1};

// Fill the aura funnel from one scan. Kept next to targetReject so the reason
// list and the counter list cannot drift apart.
void auraCountReject(Reject why) {
    switch (why) {
        case Reject::Ok: break;
        case Reject::NullOrSelf: --g_auraStats.actors; break;
        case Reject::NotAttackable: ++g_auraStats.notAttackable; break;
        case Reject::Dead: ++g_auraStats.dead; break;
        case Reject::TypeBlocked: ++g_auraStats.typeBlocked; break;
        case Reject::TypeMismatch: ++g_auraStats.typeMismatch; break;
        case Reject::NoPosition: ++g_auraStats.noPosition; break;
        case Reject::OutOfRange: ++g_auraStats.outOfRange; break;
    }
}

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

// ── Rotation (r40): a POINTER FIELD, not an ECS pool element ──────────────
//   float* rot = *(float**)(actor + 0x218);   rot[0] = yaw, rot[1] = pitch
// This is the same Vec2 the ported Melody math already produces, so no
// unit conversion sits between the aim calculation and the write. It replaces
// the old ActorRotationComponent-pool hunt (those constants stay 0 and unused).
float* rotationVec2(void* player) {
    if (!player) return nullptr;
    void* raw = nullptr;
    if (!memory::NativeMemory::instance().read(
            reinterpret_cast<std::uintptr_t>(player) + RotationPtrField, &raw, sizeof(raw))) {
        return nullptr;
    }
    if (!raw) return nullptr;
    auto* rot = static_cast<float*>(raw);
    if (!memory::NativeMemory::instance().read(
            reinterpret_cast<std::uintptr_t>(rot), rot, sizeof(float) * 2)) {
        return nullptr;
    }
    return rot;
}

bool rotationWritable() {
    return g_rotationReady.load(std::memory_order_relaxed);
}

// Apply an aim. `bodyOnly` turns the body/head without touching the camera
// rotation component — that is the closest thing to a silent rotation that
// exists in-process, where there is no packet to rewrite.
bool applyAim(void* player, float yaw, float pitch, bool bodyOnly) {
    if (!player || !std::isfinite(yaw) || !std::isfinite(pitch)) return false;

    if (bodyOnly) {
        if (!setYBodyRot || !setYHeadRot) return false;
        setYBodyRot(player, yaw);
        setYHeadRot(player, yaw);
        return true;
    }

    float* rot = rotationVec2(player);
    if (!rot) return false;
    rot[0] = yaw;
    rot[1] = pitch;
    // Mirror onto the head so the entity the server sees is looking where we
    // aimed, not where the camera used to point.
    if (setYHeadRot) setYHeadRot(player, yaw);
    return true;
}

// ── Abilities (Fly) ───────────────────────────────────────────────────────
void* abilityLayer(void* player) {
    if (!player || !getAbilities || !getLayer) return nullptr;
    void* abilities = getAbilities(player);
    if (!abilities) return nullptr;
    return getLayer(abilities, AbilityLayerBase);
}

bool setAbilityOn(void* player, int index, bool on) {
    void* layer = abilityLayer(player);
    if (!layer || !setAbility) return false;
    setAbility(layer, index, on);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Fly (r40) — flight through the player's ABILITIES, not through memory writes
//
// Why this and not the old mechanism: writing the state vector's velocity (the
// deleted native Fly, and what the Kotlin MotionFly ended up doing) fought the
// engine's own movement every tick, produced no flight animation, and told the
// server nothing — the server kept simulating a walking player. The engine has
// a first-class concept for this: the player's abilities. Permission to fly is
// an ability, being in flight is an ability, and once they are set the ENGINE
// flies the player — collision, motion, animation, and the state the server
// actually trusts. Nothing is written to velocity or position.
//
//   Mode Creative  grant MayFly only: double-tap jump to fly, vanilla creative
//                  behaviour (the form servers tolerate best).
//   Mode Force     grant Flying too: airborne immediately.
//   Ladder         setClimbing(true) while enabled — climb walls like a ladder.
//
// Give-back is not optional. Leaving Flying set after the module is switched
// off is the difference between "module off" and "still flying with the UI
// closed", so the clear runs even when the world gate would have returned early.
// ════════════════════════════════════════════════════════════════════════════
void tickFly(void* player) {
    if (!player) return;
    if (!getAbilities || !getLayer || !setAbility) return;   // underived: inert

    const int mode = mgr().enumSetting("Fly", "Mode", 0);        // 0 Creative, 1 Force
    const bool ladder = mgr().boolSetting("Fly", "Ladder", false);

    if (!abilityLayer(player)) return;
    if (!setAbilityOn(player, AbilityMayFly, true)) return;
    if (mode == 1) setAbilityOn(player, AbilityFlying, true);
    if (ladder && setClimbing) setClimbing(player, true);
    g_flyEngaged = true;
}

void tickFlyDisable(void* player) {
    if (!g_flyEngaged) return;
    if (!player) { g_flyEngaged = false; return; }
    // Apollon's guard, and it is the correct one: in a gamemode where the
    // ability was never legitimately granted (survival on a server), clearing
    // is what the server expects. In creative the ability is the game's own
    // state, so leave it alone.
    if (isAttackable && !isAttackable(player)) { g_flyEngaged = false; return; }
    setAbilityOn(player, AbilityFlying, false);
    setAbilityOn(player, AbilityMayFly, false);
    if (setClimbing) setClimbing(player, false);
    g_flyEngaged = false;
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
        // The funnel: `killauraTargetValid` only answers yes/no, which is why a
        // failed scan was anonymous. Ask targetReject directly so the reason can
        // be reported (see diagLine()).
        g_auraStats = AuraStats{};
        g_auraStats.playersOnly = playersOnly;
        g_auraStats.rangeX10 = static_cast<int>(effectiveRange * 10.0f);
        for (void* actor : getRuntimeActors(level)) {
            ++g_auraStats.actors;
            const Reject why = targetReject(player, actor, myPos, effectiveRange,
                                            playersOnly, myTypeId, 0, nullptr);
            if (why != Reject::Ok) { auraCountReject(why); continue; }
            if (useHurttime && recentlyHurt(actor, getHealth(actor))) {
                ++g_auraStats.hurtFiltered;
                continue;
            }
            ++g_auraStats.ok;
            Vec3 pos{};
            getPosition(actor, pos);
            const float dx = pos.x - myPos.x, dy = pos.y - myPos.y, dz = pos.z - myPos.z;
            targets.emplace_back(dx * dx + dy * dy + dz * dz, actor);
        }
        g_auraStats.scanned = true;
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

        // Melody's Fruit rotations, now actually applied (r40):
        //   Silent -> aim the camera rotation (attacks land where we aim)
        //   Strafe -> body/head only, camera left alone
        // The aim is written BEFORE the attack in the same tick, so the swing
        // is evaluated from the aimed direction.
        const Rot rot = getRotations(myPos, targetPos);
        if (rotationMode != 0 && rotationWritable()) {
            applyAim(player, rot.yaw, rot.pitch, /*bodyOnly=*/rotationMode == 2);
        }

        attack(gameMode, target, true);
        g_auraAttacks.fetch_add(1, std::memory_order_relaxed);
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

bool botRotationAvailable() {
    // True once the game thread has PROVEN the rotation pointer reads back as a
    // finite Vec2, and false the rest of the time. The bridge thread only ever
    // reads this atomic — it never touches game memory itself.
    return g_rotationReady.load(std::memory_order_relaxed);
}

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
    // r40: this is real now. The rotation is a pointer field on the player
    // (0x218 -> Vec2{yaw, pitch}), so no pool walk or stride guess is involved.
    if (!rotationWritable()) return false;
    return applyAim(player, yaw, pitch, /*bodyOnly=*/false);
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

    // A zero offset would resolve to the module base and call garbage, so every
    // API pointer goes through apiPtr(), which yields null and fails inert.
    getRuntimeActors = apiPtr<GetRuntimeActorFn>(GetRuntimeActor);
    attack = apiPtr<AttackFn>(Attack);
    isAttackable = apiPtr<IsAttackableFn>(IsAttackable);
    getHealth = apiPtr<GetHealthFn>(GetHealth);
    getEntityTypeId = apiPtr<GetEntityTypeIdFn>(GetEntityTypeId);

    // r40: the game's own ability / rotation / movement accessors. These are
    // what make Fly and the KillAura2 rotations possible without a single
    // velocity write.
    getAbilities = apiPtr<GetAbilitiesFn>(GetAbilities);
    getLayer = apiPtr<GetLayerFn>(GetLayer);
    setAbility = apiPtr<SetAbilityFn>(SetAbility);
    setClimbing = apiPtr<SetClimbingFn>(SetClimbing);
    setYHeadRot = apiPtr<SetYHeadRotFn>(SetYHeadRot);
    getYHeadRot = apiPtr<GetYHeadRotFn>(GetYHeadRot);
    setYBodyRot = apiPtr<SetYBodyRotFn>(SetYBodyRot);
    g_flyEngaged = false;
    g_rotationReady.store(false, std::memory_order_relaxed);

    resetState();
    // Bot data plane (r27): a fresh bind means a fresh world — drop every
    // stale actor pointer and health baseline instead of comparing against
    // a previous session's numbers.
    g_botHealth.clear();
    g_botActors.clear();
    g_botSelfDamageTicks.clear();
    g_botSelfLastHealth = -1;
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "modules ready: KillAura2 + ECSDump + Fly (ability-based, r40)");
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "api: abilities=%d layer=%d setAbility=%d rotationField=0x%llx",
        getAbilities != nullptr, getLayer != nullptr, setAbility != nullptr,
        static_cast<unsigned long long>(RotationPtrField));
    return true;
}

void onNormalTick(void* localPlayer) {
    if (!localPlayer) return;
    ++g_tick;

    // Stability gate: skip ALL memory-touching modules while the world/server
    // join rebuilds the actor structures (this was the join-crash root cause).
    // Fly off-state is handled BEFORE the world gate: switching the module off
    // must give the abilities back even while a world is rebuilding.
    if (!mgr().enabled("Fly")) tickFlyDisable(localPlayer);

    if (!worldReady(localPlayer)) {
        // Let a disabled ECSDump re-arm so the next enable starts fresh.
        if (!mgr().enabled("ECSDump")) { g_dumped = false; g_dumpStartTick = -1; }
        // Not in a world: nothing the pool cache says is trustworthy next time.
        dropTypePoolCache();
        return;
    }

    // Rotation readiness: probed here (game thread) and published for the
    // bridge thread. Re-probed occasionally so a world change cannot leave a
    // stale "true" behind.
    if (!g_rotationReady.load(std::memory_order_relaxed) || (g_tick % 200) == 0) {
        g_rotationReady.store(rotationVec2(localPlayer) != nullptr, std::memory_order_relaxed);
    }

    // Prune hurt-state map of stale pointers occasionally.
    if ((g_tick & 0x3FF) == 0 && g_hurtState.size() > 512) g_hurtState.clear();

    // Loopback agent (r36): latch this tick's world state for the companion app,
    // then execute whatever intents it queued. publish() runs FIRST so the actor
    // order the app receives is the same order the attacks below index into.
    // Both are no-ops unless a client is connected.
    agentstate::publish(localPlayer);
    agentstate::executeQueued(localPlayer);

    if (mgr().enabled("Fly")) tickFly(localPlayer);

    if (mgr().enabled("KillAura2")) tickKillAura2(localPlayer);

    // Kotlin-language modules (Hitbox / AutoTotem today, the PvP bot next): one
    // JNI dispatch per tick. Shares this tick's world-stability gate, so Kotlin
    // modules never touch memory while the world is still rebuilding.
    if (kotlinbridge::anyKotlinModuleEnabled()) kotlinbridge::dispatchTick(localPlayer);

    if (mgr().enabled("ECSDump")) tickEcsDump(localPlayer);
}


// ════════════════════════════════════════════════════════════════════════════
// diagLine() — the in-game panel's instrument panel
//
// Built because of a real report: "essentially no module works, not even the
// killaura". Every stage between "the switch is on" and "a hit lands" had a
// silent failure path, so there was no way to tell which one fired. This prints
// them all, in the order they can fail:
//
//   tick 12480  api 6/6  world ok  on: KillAura2,Fly
//   aura[3.0]: seen 12  ok 1  noatk 0  dead 0  rng 11  hurt 0  type 0+0  nopos 0  attacks 3
//
// Reading it:
//   tick not advancing      → the game thread never runs our hook (nothing works)
//   api 5/6                 → a game API pointer did not resolve; every module
//                             that needs it refuses to run
//   world no                → the world-stability gate is holding everything back
//   on: (none)              → nothing is enabled: a UI/toggle problem, not a module
//   seen 0                  → the actor list is empty: no entities, or a bad list
//   seen 12  ok 0  noatk 12 → every actor is filtered by isAttackable — the module
//                             is fine, the game refuses the target (gamemode)
//   seen 12  ok 1  rng 11   → the funnel works; the targets are simply far away
//   ok 3  attacks 0         → the attack was never reached (delay / hurttime filter)
//   ok 3  attacks 40        → we ARE attacking: look at the server, not the client
// ════════════════════════════════════════════════════════════════════════════
std::string diagLine() {
    const AuraStats s = g_auraStats;   // snapshot; the game thread may be mid-scan

    int apiOk = 0;
    apiOk += getRuntimeActors ? 1 : 0;
    apiOk += attack ? 1 : 0;
    apiOk += isAttackable ? 1 : 0;
    apiOk += getHealth ? 1 : 0;
    apiOk += getPosition ? 1 : 0;
    apiOk += getEntityTypeId ? 1 : 0;

    std::string on;
    for (const auto& mod : mgr().all()) {
        if (!mod.enabled) continue;
        if (!on.empty()) on += ',';
        on += mod.name;
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf), "tick %ld  api %d/6  world %s  on: %s",
                  static_cast<long>(g_tick), apiOk, g_stableLatch ? "ok" : "no",
                  on.empty() ? "(none)" : on.c_str());

    char a[384];
    if (!s.scanned) {
        std::snprintf(a, sizeof(a), "aura: no scan yet (KillAura2 %s)",
                      mgr().enabled("KillAura2") ? "enabled" : "off");
    } else {
        std::snprintf(a, sizeof(a),
            "aura[%.1f%s]: seen %d  ok %d  noatk %d  dead %d  rng %d  hurt %d  "
            "type %d+%d  nopos %d  attacks %ld",
            s.rangeX10 / 10.0, s.playersOnly ? ",players" : "",
            s.actors, s.ok, s.notAttackable, s.dead, s.outOfRange, s.hurtFiltered,
            s.typeBlocked, s.typeMismatch, s.noPosition,
            g_auraAttacks.load(std::memory_order_relaxed));
    }

    std::string out = buf;
    out += '\n';
    out += a;
    return out;
}

} // namespace eclient_runtime::modules::melody
