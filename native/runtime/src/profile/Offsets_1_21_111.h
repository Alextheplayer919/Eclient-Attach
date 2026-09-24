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
}
