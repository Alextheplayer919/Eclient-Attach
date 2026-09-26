#pragma once

#include <cstdint>
#include <string>

namespace eclient_runtime::modules::melody {

// Melody client module ports (KillAura2, velocity Fly) + the ECSDump
// diagnostic module. Bound once the runtime located libminecraftpe.so; runs
// from the LocalPlayer::normalTick hook on the game thread.
bool initialize(std::uintptr_t minecraftBase);
void onNormalTick(void* localPlayer);

// ── Bot support (r27): the data plane for the Kotlin PVP bot ──────────────
//
// Decisions live in Kotlin (kotlin-modules/.../PvpBot.kt) because that is where
// score tables, exploration and persistence belong. This layer only answers the
// questions a decision needs, and it answers them with the SAME validated
// offsets and the SAME actor filter as KillAura2 — the bot can never "see" an
// actor the module itself would refuse to attack.
//
// Indices refer to the order of the MOST RECENT botScan and are only valid for
// that tick (the next scan reorders them). That is exactly how Kotlin uses
// them: scan → aim/attack in the same tick.
struct BotActor {
    float x, y, z;         // world position
    float vx, vy, vz;      // velocity
    float distance;        // to the local player
    int health;            // GetHealth; 0 when unavailable
    std::uint32_t typeId;  // GetEntityTypeId (player type id identifies players)
    std::uint32_t entityId;// packed entity id — STABLE identity across ticks
    int flags;             // kBotFlag* bits below
};

inline constexpr int kBotFlagHurt = 1;       // damaged within the last 10 ticks
inline constexpr int kBotFlagAttackable = 2;

// Fill `out` (cap entries) with nearby attackable actors, nearest first.
// Returns the count, or -1 when the world is not readable (fail inert).
//
// mode: 0 = players only (strict)
//       1 = players only, but if the filter matches nobody while other
//           attackable actors ARE in range, retry without it and flag the
//           fallback (a bot that sees an empty world is worse than a bot that
//           might swing at a mob once)
//       2 = anything attackable
int botScan(void* player, float maxRange, int mode, BotActor* out, int cap);

// One line describing the LAST scan: counts per rejection stage plus sampled
// type ids and which path produced them. This is what makes "the bot detects
// nobody" a specific, checkable statement instead of a guess.
std::string botScanDiag();

// Attack the actor at `index` of the last scan (the game's own attack call).
bool botAttackIndex(void* player, int index);

// Yaw/pitch (degrees) that faces the actor at `index`. out[0]=yaw, out[1]=pitch.
bool botAimAt(void* player, int index, float* outYawPitch);

// NOTE: movement is deliberately NOT here. The velocity write lives in
// KotlinBridge.cpp (botMove native) because that is where g_prevOut — the
// record of the velocity WE wrote — is kept, and the input-residue math needs
// it to tell the human's own movement apart from the bot's.

// Self state: out[0..2]=pos, out[3..5]=velocity, out[6]=health,
// out[7]=damaged within 10 ticks (1/0), out[8]=damage events in the last 5 s,
// out[9]=ticks since the last damage event (-1 = never).
bool botSelf(void* player, float* outSelf);

// The player's current yaw in degrees, or NaN when the rotation element layout
// is not derived yet. Walking needs it: joystick input is camera-relative, so
// world-space intent cannot be turned into input axes without it.
float botPlayerYaw(void* player);

// Whether aim can actually be APPLIED (ActorRotationComponent derived).
// False today — the bot computes the aim it needs but cannot turn the player
// yet; callers must show that instead of pretending to aim.
bool botRotationAvailable();

// ── Seam for the loopback agent (r36 / docs/MERGE_PLAN.md P0) ──────────────
//
// The agent speaks to an app that must never touch game memory, so it needs
// three things this layer already knows how to answer, exposed as functions
// instead of internals:
//
//   * who counts as a player (the masked type comparison),
//   * whether a rotation write is possible (same guard the aim path uses),
//   * and a single place to apply an externally chosen yaw/pitch.
//
// All three fail closed: an underived layout answers "no", never "sure".

// The local player's own type id as resolved by the LAST botScan (the value the
// player filter compares against). `ok` (optional) receives whether it was
// readable at all. Returns 0 when unknown.
std::uint32_t botMyTypeId(bool* ok);

// True when `typeId` is the same (masked) type as the local player's — i.e. the
// actor is a player. False when our own type is not resolved yet, so callers
// must not treat "false" as "definitely not a player" without checking ok.
bool botIsPlayerType(std::uint32_t typeId);

// Apply an externally chosen rotation (degrees). Returns false when the rotation
// element layout is not derived yet — the body lands with the derivation, and
// the guard is what makes saying so the only possible answer today.
bool botApplyRotation(void* player, float yaw, float pitch);

// Apply joystick-style input (forward/strafe in -1..1). Returns false until the
// MoveInputComponent element layout is derived.
bool botApplyInput(void* player, float forward, float strafe, bool jump, bool sneak);

} // namespace eclient_runtime::modules::melody
