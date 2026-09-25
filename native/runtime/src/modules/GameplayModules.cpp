#include "modules/GameplayModules.h"
#include "modules/MelodyModules.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"
#include "memory/NativeMemory.h"
#include "dobby.h"
#include <android/log.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eclient_runtime::modules::gameplay {
namespace {
constexpr const char* TAG = "EClientGameplay";
using namespace eclient_runtime::profile::mc_1_21_111;

using NormalTickFn = void* (*)(void*);
using SpeedFn = void (*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*);

NormalTickFn oldNormalTick = nullptr;
SpeedFn oldSpeed = nullptr;
std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }
inline bool on(const char* n) { return ModuleManager::instance().enabled(n); }

// ---------------------------------------------------------------------------
// AlwaysSprint (rewritten r25)
//
// The previous version called SetSprinting(player, true) on EVERY tick. That is
// exactly what broke gapples: while an item is in use the engine sets sprint
// false each tick, and re-forcing it fights the item-use tick, so the eat never
// completes.
//
// The rewrite refuses to fight the engine. It only re-asserts sprint when the
// player is provably NOT using an item. That state is not derived yet, so the
// module stays INERT — fail-closed, the same policy the memory patches use —
// instead of silently breaking item use. The seam is these two constants in
// profile/Offsets_1_21_111.h; when they land with disassembly evidence this
// starts working with no other change.
// ---------------------------------------------------------------------------

bool itemUseStateKnown() {
    return UsingItemFlagOffset != 0 && UsingItemFlagMask != 0;
}

// true  = provably not using an item (safe to force sprint)
// false = unknown, unreadable, or item in use (hands off)
bool sprintSafeFor(void* player) {
    if (!player || !itemUseStateKnown()) return false;
    std::uint32_t flags = 0;
    if (!memory::NativeMemory::instance().read(
            reinterpret_cast<std::uintptr_t>(player) + UsingItemFlagOffset,
            &flags, sizeof(flags))) {
        return false;
    }
    return (flags & UsingItemFlagMask) == 0;
}

void* hookedNormalTick(void* player) {
    void* result = oldNormalTick ? oldNormalTick(player) : nullptr;
    if (!player) return result;

    if (on("AlwaysSprint") && sprintSafeFor(player)) {
        using Fn = void (*)(void*, bool);
        reinterpret_cast<Fn>(fn(SetSprinting))(player, true);
    }

    // Melody ports (KillAura2, ECSDump) run after the vanilla-behaving logic
    // above; the Kotlin module dispatch happens inside melody::onNormalTick so
    // everything shares one world-stability gate.
    melody::onNormalTick(player);
    return result;
}

// Fixed: original was called twice when Speed was enabled.
// Call the original exactly once; actual speed modification logic
// still needs the proper velocity write once Apollon conventions are wired.
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

    if (!melody::initialize(base)) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Melody modules failed to bind");
        return false;
    }
    if (DobbyHook(fn(NormalTick), reinterpret_cast<void*>(hookedNormalTick), reinterpret_cast<void**>(&oldNormalTick)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "NormalTick hook failed");
        return false;
    }
    // SpeedHack stays hooked as a pure pass-through ON PURPOSE: the hook set is
    // part of the verified boot chain (docs/DO_NOT_BREAK.md). The Speed module
    // is no longer exposed, so this only ever calls the original exactly once.
    if (DobbyHook(fn(SpeedHack), reinterpret_cast<void*>(hookedSpeed), reinterpret_cast<void**>(&oldSpeed)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Speed hook failed");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "1.21.111 gameplay hooks installed: NormalTick + SpeedHack(passthrough)");
    return true;
}
void shutdown() {}
} // namespace eclient_runtime::modules::gameplay
