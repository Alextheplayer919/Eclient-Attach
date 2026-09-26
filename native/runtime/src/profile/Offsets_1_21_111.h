#pragma once
#include <cstdint>

// Minecraft Bedrock 1.21.111 ARM64 profile.
// Source: user-supplied Apollon 1.21.111 offset table, validated against
// the user-supplied libminecraftpe.so by checking the referenced addresses
// fall inside the executable image and match the expected entry bytes.
namespace eclient_runtime::profile::mc_1_21_111 {
// Real GNU build ID of the matched libminecraftpe.so (string form used by verify tools):
// ea5614dcc3551721b14f9b4686aa1651c1b8f79e
inline constexpr std::uint64_t BuildId = 0x0; // numeric placeholder; string lives in verify scripts
inline constexpr std::uint64_t NoHurtCam = 0x05103870;
inline constexpr std::uint64_t NoCamDistortion = 0x0DF56A14;
inline constexpr std::uint64_t NoBoatRotation = 0x05114BB8;
inline constexpr std::uint64_t NoCamSleep = 0x0DF57550;
inline constexpr std::uint64_t PlaceCamera = 0x0E1DC87C;
inline constexpr std::uint64_t SlowDownTriggers = 0x0954C0E4;
inline constexpr std::uint64_t NoSlowDown = 0x0945D230;
inline constexpr std::uint64_t WaterDrown = 0x09DB7558;
inline constexpr std::uint64_t LavaDrown = 0x09DB9528;
inline constexpr std::uint64_t Noclip = 0x093D2760;
inline constexpr std::uint64_t AutoClickMine = 0x0CCC2258;
inline constexpr std::uint64_t XrayCameraThird = 0x0E1DB2A4;
inline constexpr std::uint64_t IsEmoting = 0x0CE072A8;
inline constexpr std::uint64_t NoBlur1 = 0x07BBE1E4;
inline constexpr std::uint64_t NoBlur2 = 0x07BBEAB4;
inline constexpr std::uint64_t VignetteRenderer = 0x06376084;
inline constexpr std::uint64_t LerpMotion = 0x0C947CDC;
inline constexpr std::uint64_t FullBright = 0x06C6B95C;
inline constexpr std::uint64_t SpeedHack = 0x09498748;
inline constexpr std::uint64_t JumpHack = 0x09D8D7F0;
inline constexpr std::uint64_t WaterSpeed = 0x09E52A70;
inline constexpr std::uint64_t LavaSpeed = 0x09407F30;
// Corrected from Apollon 1.21.111 (was incorrectly 0x09DB5D98)
inline constexpr std::uint64_t Gravity = 0x09D88C20;
inline constexpr std::uint64_t Attack = 0x0CCC075C;
inline constexpr std::uint64_t IsAttackable = 0x0C95B500;
inline constexpr std::uint64_t NormalTick = 0x06BEDA24;
inline constexpr std::uint64_t GetRuntimeActor = 0x0D6B0D68;
inline constexpr std::uint64_t GetEntityTypeId = 0x0C937698;
inline constexpr std::uint64_t GetHealth = 0x0C9437D0;
// From Apollon 1.21.111 (previously missing — caused compile failure)
inline constexpr std::uint64_t SetSprinting = 0x6BF05B8;
inline constexpr std::uint64_t InitMaxAutoStep = 0xC956C54;
// Ability helpers used by Fly. Offsets must come from the same Apollon table;
// left at 0 until the exact values are confirmed against the binary so that
// initialize() keeps the function pointers null and Fly stays inert.
inline constexpr std::uint64_t GetAbilities = 0x0;
inline constexpr std::uint64_t GetLayer = 0x0;
inline constexpr std::uint64_t SetAbility = 0x0;
inline constexpr std::uint64_t GetPositionField = 0x208;
inline constexpr std::uint64_t GetLevelField = 0x1D0;
inline constexpr std::uint64_t GetGameModeField = 0x9C0;
inline constexpr std::uint64_t GetNameField = 0xB30;

// ---------------------------------------------------------------------------
// Offsets below were DERIVED FROM THE 1.21.111 BINARY by Capstone disassembly
// of the shipped libminecraftpe.so (SHA-256-verified APK). Each entry lists
// the instruction-level evidence. They are NOT from the Apollon table.
// ---------------------------------------------------------------------------

// LerpMotion @ 0x0C947CDC:
//   ldr  x8, [x0, #0x208]          ; x8 = actor->stateVector
//   ldr  w9, [x1, #8] / ldr x10, [x1]
//   str  w9, [x8, #0x20]           ; velocity.z at stateVector+0x20
//   str  x10, [x8, #0x18]          ; velocity.x/y at stateVector+0x18
// => *reinterpret_cast<void**>(actor + 0x208) is the state-vector component,
//    layout matches Melody's StateVectorComponent: pos @+0x0, prevPos @+0x0C,
//    velocity Vec3 @+0x18. Runtime gate: only write when finite and |v|<100.
inline constexpr std::uint64_t VelocityField = 0x18;

// GetHealth stub @ 0x0C9437D0 (add x0, x0, #8; b 0x0CDF16D8) and its
// implementation @ 0x0CDF16D8:
//   ldr  x9, [x0, #8]              ; entity registry pointer
//   ldr  w8, [x0, #0x10]           ; entity id (u32 packed)
// With x0 == actor+8 these become actor offsets 0x10 / 0x18, independently
// re-confirmed by two more functions using the identical prologue:
//   SetSprinting @ 0x6BF05B8:  ldr x9, [x20, #0x10]   (registry)
//   ECS getter   @ 0x6BEE43C:  ldr x9, [x0, #0x10] / ldr w8, [x0, #0x18]
// => ECS registry pointer at actor+0x10, packed entity id (u32) at actor+0x18.
inline constexpr std::uint64_t EcsRegistryField = 0x10;
inline constexpr std::uint64_t EntityIdField = 0x18;

// GetEntityTypeId @ 0x0C937698 consists of (bl 0x0C950F38; ldr w0, [x0]; ret),
// i.e. a pure getter returning a u32 type id. Used at runtime as the player
// classifier: typeId(localPlayer) == 319 ("minecraft:player") on 1.21,
// matching Melody's player-type check; other actors can be classified by
// comparing against the local player's own id instead of hardcoding 319.

// ---------------------------------------------------------------------------
// PvP build (r25) — PENDING DERIVATION.
//
// Every entry here is a SEAM, not a guess. Consumers MUST read the zero as
// "feature not unlocked" and fail inert, exactly like the ECS component hashes
// below. Nothing here is filled in until it has instruction-level evidence from
// the SHA-verified libminecraftpe.so.
// ---------------------------------------------------------------------------

// AlwaysSprint guard: the actor's "item in use" state. Re-forcing sprint while
// an item is in use is what broke gapples, so AlwaysSprint refuses to act until
// this is derived. The shape (actor flag word vs ECS component element) gets
// pinned here when the evidence lands.
inline constexpr std::uint64_t UsingItemFlagOffset = 0; // 0 = underived
inline constexpr std::uint32_t UsingItemFlagMask = 0;   // 0 = underived

// ---------------------------------------------------------------------------
// ECS COMPONENT HASHES (r28) — DERIVED, NOT GUESSED.
//
// METHOD (see tools/entt_hash.py, which reproduces this offline):
// enTT gives every component a pool id = FNV-1a 32 over the component's PLAIN
// TYPE NAME (the same spelling that appears inside the game's own mangled
// names, e.g. `N4entt13basic_storageI22ActorRotationComponent...`).
//
// VERIFICATION against the shipped, SHA-pinned libminecraftpe.so:
//   * FNV1a("ActorRotationComponent") == 0x75df36b7 occurs 38 times.
//     A 4-byte value of that length in a 280 MB image is expected to occur
//     ~0.06 times by chance, so the match is not coincidence.
//   * Control: FNV1a("struct ActorRotationComponent") == 0xdc1e21ce occurs ZERO
//     times, i.e. the name used really is the plain type name.
//   * The constants sit in .rodata inside the component-hash lists that belong
//     to the game's strict-system expressions, next to the mangled names of the
//     systems that read/write those very components (ActorHeadRotation 0xbabe7211
//     appears immediately beside ActorRotation 0x75df36b7 in one such list).
//
// So the POOL a component lives in is now a solved lookup. What is still NOT
// solved is the ELEMENT layout inside that pool (stride + field offsets); those
// stay 0 and every consumer must keep failing inert until ECSDump has pinned
// them on a device (rotate the view / walk around and read the deltas).
// ---------------------------------------------------------------------------
inline constexpr std::uint32_t ActorRotationComponentHash = 0x75df36b7;     // rotation {Vec2 yaw/pitch, ...}
inline constexpr std::uint32_t ActorHeadRotationComponentHash = 0xbabe7211; // head/aim rotation
inline constexpr std::uint32_t MobHurtTimeComponentHash = 0xd7a3585c;       // 0.5 s damage immunity
inline constexpr std::uint32_t MoveInputComponentHash = 0x018b1887;         // the joystick's own state
inline constexpr std::uint32_t RawMoveInputComponentHash = 0x3613e513;      // raw (pre-deadzone) input
inline constexpr std::uint32_t HitboxComponentHash = 0x9b97c2b2;            // hitbox width/height
// Negative result worth keeping: FNV1a("ItemInUseComponent") == 0x05b90aef
// occurs ZERO times, i.e. no storage is instantiated under that name. The
// item-use state AlwaysSprint waits for therefore lives somewhere else (an
// actor flag word, or a differently named component) — do not "fix" this by
// trying name variants; it needs evidence.

// Element layout — STILL UNDERIVED. Never set one of these without the others:
// the KillAura2 rotation write indexes block + inBlock*stride + yawOffset, so a
// non-zero hash with a zero stride would write into the component block itself.
inline constexpr std::uint64_t ActorRotationStride = 0;        // element stride in pool block
inline constexpr std::uint64_t ActorRotationYawOffset = 0;     // offset of Vec2 yaw inside element
inline constexpr std::uint64_t HitboxStride = 0;
inline constexpr std::uint64_t HitboxWidthOffset = 0;

// ---------------------------------------------------------------------------
// ACTOR TYPE COMPONENT (r29) — DERIVED, stride INCLUDED.
//
// An actor's numeric type id is not a method: GetEntityTypeId @0x0C937698 is
// just (bl 0x0C950F38; ldr w0, [x0]; ret), and its callee resolves an ECS
// component through the standard page/packed chain with the wanted hash
// loaded as an immediate: `mov w10,#0xa419 / movk w10,#0x4f6b,lsl#16`, i.e.
// 0x4f6ba419. That is FNV-1a over "ActorTypeComponent" (verified against the
// binary, and the only name in the 619 enTT storages that produces it).
//
// The element address in that callee ends with:
//     and x9, x10, #0x3ffff / and x9, x9, #0x7f / add x0, x8, x9, lsl #2
// i.e. element = block + (packed & 0x7F) * 4 — a plain 4-byte component. So
// the type id can be read directly, with our own guarded memory reads, and
// without depending on a function pointer's ABI.
// ---------------------------------------------------------------------------
inline constexpr std::uint32_t ActorTypeComponentHash = 0x4f6ba419;
inline constexpr std::uint64_t ActorTypeComponentStride = 4; // disasm: `lsl #2`

// The returned value is compared MASKED. Rationale: the "not attackable" list
// Melody shipped contains entries like 4194372 (0x400044) and 12582992
// (0xC00090) — small ids with high flag bits set — so equal types can differ in
// those bits. Masking only ever makes equality MORE permissive, which is the
// safe direction here: a false positive costs one wasted swing, a false
// negative made KillAura2 and the bot see an empty world.
inline constexpr std::uint32_t TypeIdMask = 0x3FFFFF;

// ---------------------------------------------------------------------------
// MOVEMENT INPUT (r28) — SEAM, underived.
//
// The bot walks by writing the SAME input state the on-screen joystick writes
// and letting the engine do the rest: speed, friction, collision, sprint
// rules, animations, footsteps. That is unmodified player walking, and it is
// what the product wants — the previous approach (writing the state vector's
// velocity, i.e. the fly mechanism) fought the engine every tick, produced no
// walk animation and could not respect the engine's own rules. It is deleted.
//
// Dependencies, both required before `walking` can be true:
//   * the MoveInputComponent element layout (below), and
//   * the player's current yaw (ActorRotationComponent layout), because input
//     is camera-relative while the bot thinks in target-relative terms.
// Until both are derived, movement is INERT and the bot says so.
// ---------------------------------------------------------------------------
inline constexpr std::uint64_t MoveInputElementStride = 0; // element stride in the pool block
inline constexpr std::uint64_t MoveInputVecOffset = 0;   // 2 floats (forward/strafe) in the element
inline constexpr std::uint64_t MoveInputFlagOffset = 0;  // flags byte/word in the element (0 = none found yet)
inline constexpr std::uint32_t MoveInputJumpBit = 0;     // bit masks, meaningful only with the offset
inline constexpr std::uint32_t MoveInputSneakBit = 0;
inline constexpr std::uint32_t MoveInputSprintBit = 0;
inline constexpr int MoveInputForwardSign = 0;           // +1/-1 axis convention (0 = not pinned)
inline constexpr int MoveInputStrafeSign = 0;
}
