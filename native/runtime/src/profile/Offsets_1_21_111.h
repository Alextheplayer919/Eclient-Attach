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
// ── Ability helpers (Fly) — CONFIRMED, no longer 0 ───────────────────────
// Source: the Apollon v4.62 1.21.111 table now in the repo (see the section at
// the end of this file). Cross-validated before use: every entry we derived
// independently — LerpMotion, JumpHack, Gravity, InitMaxAutoStep,
// GetPositionField, GetLevelField, GetGameModeField, GetNameField, Attack,
// IsAttackable — matches that table exactly, so both describe this binary.
inline constexpr std::uint64_t GetAbilities = 0x0CDF943C; // Player::getAbilities
inline constexpr std::uint64_t GetLayer = 0x0CDF9450;     // LayeredAbilities::getLayer
inline constexpr std::uint64_t SetAbility = 0x0CDF87E0;   // Abilities::setAbility
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

// ===========================================================================
// APOLLON v4.62 (1.21.111) — API POINTERS, now IN THE REPO
//
// Why these are trusted: the same table supplied GetAbilities/GetLayer/
// SetAbility and the ability-based fly, and its Fields64 block reproduces our
// independently derived field offsets exactly (GetPosition=0x208, GetLevel=0x1D0,
// GetGamemode=0x9C0, GetName=0xB30, and GetAbilities/GetLayer/SetAbility line up
// with the Fly mechanism our header had been waiting on). A table that agrees
// with our Capstone-derived entries on every shared symbol is describing the
// same libminecraftpe.so.
//
// Everything below is a CALLABLE (function pointer), unlike the offsets above.
// Resolve through fn() exactly like Attack/IsAttackable, and keep the
// "if (!ptr) return;" guard: a null function pointer must fail inert.
// ===========================================================================

// ── movement / flight ──────────────────────────────────────────────────────
inline constexpr std::uint64_t SetClimbing = 0x0C945610;   // Actor::setClimbing
inline constexpr std::uint64_t SetOnGround = 0x06F42DFC;   // ActorEntityAccess::addOrRemoveComponent<OnGroundFlagComponent>
inline constexpr std::uint64_t SetNoJumpDelay = 0x0CDDB410;// MobJump::setNoJumpDelay
inline constexpr std::uint64_t MobIsJumping = 0x0CDD8840;  // ActorEntityAccess::addOrRemoveComponent<MobIsJumpingFlag>

// ── rotation ───────────────────────────────────────────────────────────────
// The player's rotation is a POINTER FIELD, not a pool element:
//     float* rot = *(float**)(actor + RotationPtrField);
//     rot[0] = yaw (degrees), rot[1] = pitch   <- identical Vec2 order to the
//                                                Melody math already ported
// This supersedes the old plan of hunting ActorRotationComponent through the
// ECS pool (ActorRotationStride/YawOffset stay 0 and are now unused).
inline constexpr std::uint64_t RotationPtrField = 0x218;   // Actor::setRotationX
inline constexpr std::uint64_t RotationPitchOffset = 0x04;
inline constexpr std::uint64_t SetYHeadRot = 0x0C95AF30;   // Actor::setYHeadRot
inline constexpr std::uint64_t GetYHeadRot = 0x0C95AE58;   // Actor::getYHeadRot
inline constexpr std::uint64_t SetYBodyRot = 0x0C86C5D4;   // Mob::setYBodyRotation

// ── combat / world ─────────────────────────────────────────────────────────
inline constexpr std::uint64_t GetAABBDim = 0x0C93AE58;    // Actor::getAABBDim -> Vec2{width, height}
inline constexpr std::uint64_t Intersects = 0x0C94727C;    // Actor::intersects (line of sight)
inline constexpr std::uint64_t SetInvisible = 0x0C9597B8;  // Actor::setInvisible
inline constexpr std::uint64_t Swing = 0x06BF031C;         // LocalPlayer::swing
inline constexpr std::uint64_t ForEachActivePlayer = 0x0CE9B8F4; // GameplayUserManager::forEachActivePlayer
inline constexpr std::uint64_t HurtTimeField = 0x194;      // Mob::hurtEffects

// ── ability indices for LayeredAbilities::setAbility(index, on) ─────────────
inline constexpr int AbilityLayerBase = 2;   // the layer Apollon drives
inline constexpr int AbilityFlying = 9;      // in flight right now
inline constexpr int AbilityMayFly = 10;     // allowed to start flying

// ===========================================================================
// RECOVERED APOLLON v4.62 — REFERENCE CONSTANTS (aarch64), not yet wired in
// ===========================================================================
// These are the entries of the recovered Apollon offset table that our profile
// did not already carry. Every symbol we DO share with that table agrees
// exactly (see docs/APOLLON_RECOVERED.md), which is why the remainder is worth
// keeping: it is the same libminecraftpe.so build, described by someone who had
// the client running against real servers.
//
// NOTHING here is used by a module yet. They are recorded so the next feature
// does not need the zip that keeps disappearing — and so that whoever wires one
// up starts from a value cross-checked with a working client instead of a guess.
// Treat each as a starting point to be validated by PatchManager-style entry-byte
// checks before it is trusted.
//
// Apollo resolves them as `base + offset` (getAbsoluteAddress), same as fn().
inline constexpr std::uint64_t Font = 0x0263C304;  // [fn   ] Default 1 float
inline constexpr std::uint64_t WaterTrigger1 = 0x066578D8;  // [fn   ] <WasInWaterFlagComponent,EntityId ActorHeadWasInWater
inline constexpr std::uint64_t WaterTrigger2 = 0x093B7B9C;  // [fn   ] <ActorHeadInWaterFlagComponent,EntityId SUB
inline constexpr std::uint64_t WaterTrigger3 = 0x05E35F94;  // [fn   ] <WaterSplashEffectRequestComponent,EntityId SUB
inline constexpr std::uint64_t Fov = 0x07BBDB00;  // [fn   ] 9CameraAPI returns float
inline constexpr std::uint64_t FastBridge = 0x0CCC3650;  // [fn   ] "GameMode::useItemOn (client)"
inline constexpr std::uint64_t BlockReach = 0x0CCC4B70;  // [fn   ] returns 7 or 12 float
inline constexpr std::uint64_t PlayerAuthInput = 0x0A772168;  // [fn   ] "Player`s Head rotation"
inline constexpr std::uint64_t NativeKeyHandler = 0x0CFA5130;  // [fn   ] ...nativeKeyHandler
inline constexpr std::uint64_t InMainMenu = 0x06086AB4;  // [fn   ] 23CubemapBackgroundScreen
inline constexpr std::uint64_t InLanGame1 = 0x0E26A008;  // [fn   ] NetherNet::SimpleNetworkInterfaceImpl::SendPacket
inline constexpr std::uint64_t InLanGame2 = 0x0E26A234;  // [fn   ] NetherNet::SimpleNetworkInterfaceImpl::ReadPacket
inline constexpr std::uint64_t FastDrop = 0x06358EDC;  // [fn   ] GuiData::_tickItemDrop intertnal float field
inline constexpr std::uint64_t UnlockMarket = 0x076CF620;  // [fn   ] Entitlement::isOwned
inline constexpr std::uint64_t GetMaxPlayers = 0x0E14C3C4;  // [fn   ] AppPlatform::getMaxPlayers
inline constexpr std::uint64_t FogState = 0x07C08F08;  // [fn   ] LevelRendererCamera::_getFogDensitySettingType
inline constexpr std::uint64_t SetupFog = 0x07C00420;  // [fn   ] LevelRendererPlayer::setupFog
inline constexpr std::uint64_t ShowPaperdoll = 0x06373FB8;  // [fn   ] HudPlayerRenderer::update
inline constexpr std::uint64_t Subtitle = 0x063164C4;  // [fn   ] TileMessage::setSubtitle
inline constexpr std::uint64_t ClientInstanceOnTick = 0x06478734;  // [fn   ] ClientInstance::onTick
inline constexpr std::uint64_t HandleDestroyOrAttack = 0x06462B60;  // [fn   ] ClientInputCallbacks::handleDestoryOrAttackButtonPress
inline constexpr std::uint64_t PlayUI = 0x0648AED8;  // [fn   ] ClientInstance::playUI
inline constexpr std::uint64_t DisplayMessage = 0x06BF196C;  // [fn   ] LocalPlayer::displayClientMessage
inline constexpr std::uint64_t Block_getRenderLayer = 0x0D52E0F4;  // [fn   ] Block::getRenderLayer
inline constexpr std::uint64_t RebuildAllChunkGeometry = 0x07BF10A8;  // [fn   ] LevelRenderer::rebuildAllRenderChunkGeometry
inline constexpr std::uint64_t NoNametags = 0x06AEE80C;  // [fn   ] BaseActorRenderer::renderText
inline constexpr std::uint64_t ScreenName = 0x0648482C;  // [fn   ] ClientInstance::getScreenName
inline constexpr std::uint64_t GetNameTag = 0x0C94C468;  // [fn   ] Actor::getNameTag
inline constexpr std::uint64_t GetDimensionBlockSource = 0x0C9318F0;  // [fn   ] Actor::getDimensionBlockSource
inline constexpr std::uint64_t GetBlock = 0x0CEE4E74;  // [fn   ] BlockSource::getBlock
inline constexpr std::uint64_t IsAir = 0x0CFA78FC;  // [fn   ] Block::isAir
inline constexpr std::uint64_t ActorRotation = RotationPtrField;  // alias — Apollon's name for our RotationPtrField (0x218)
inline constexpr std::uint64_t GetHurtTime = HurtTimeField;  // alias — Apollon's name for our HurtTimeField (0x194)
inline constexpr std::uint64_t GetCameraPos = 0x0000065C;  // [field] LevelRendererCamera::getCameraPos
inline constexpr std::uint64_t GetLevelRendererPlayer = 0x000003A0;  // [field] LevelRenderer::getLevelRendererPlayer
inline constexpr std::uint64_t GetLevelRenderer = 0x000000C0;  // [field] ClientInstance::getLevelRenderer
inline constexpr std::uint64_t BlockTypeClass = 0x00000078;  // [field] inside Block::getDescriptionId
inline constexpr std::uint64_t GetPlatform = 0x0000046C;  // [field] Player::getPlatform
inline constexpr std::uint64_t GetGameplayUserManager = 0x00000430;  // [field] inside Level::forEachPlayer
inline constexpr std::uint64_t ViewMatrix = 0x00000318;  // [field] ClientInstance Cheat Engine
inline constexpr std::uint64_t FovX = 0x00000F28;  // [field] LevelRendererPlayer Game Guardian 0.3~0.7 fovX
inline constexpr std::uint64_t FovY = 0x00000F3C;  // [field] LevelRendererPlayer Game Guardian 0.8~1.5 fovY
}
