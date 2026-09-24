#include "modules/GameplayModules.h"
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
using GetAbilitiesFn = void* (*)(void*);
using GetLayerFn = void* (*)(void*, int);
using SetAbilityFn = void (*)(void*, int, bool);
using GetRuntimeActorFn = std::vector<void*> (*)(void*);
using AttackFn = void (*)(void*, void*, bool);
using IsAttackableFn = bool (*)(void*);
using GetHealthFn = int (*)(void*);

NormalTickFn oldNormalTick = nullptr;
SpeedFn oldSpeed = nullptr;
GetAbilitiesFn getAbilities = nullptr;
GetLayerFn getLayer = nullptr;
SetAbilityFn setAbility = nullptr;
GetRuntimeActorFn getRuntimeActors = nullptr;
AttackFn attack = nullptr;
IsAttackableFn isAttackable = nullptr;
GetHealthFn getHealth = nullptr;
std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }
inline bool on(const char* n) { return ModuleManager::instance().enabled(n); }

void* hookedNormalTick(void* player) {
    void* result = oldNormalTick ? oldNormalTick(player) : nullptr;
    if (!player) return result;

    // Sprint and step are simple LocalPlayer calls with verified 1.21.111 addresses.
    if (on("AlwaysSprint")) {
        using Fn = void (*)(void*, bool);
        reinterpret_cast<Fn>(fn(SetSprinting))(player, true);
    }
    if (on("Step")) {
        using Fn = void (*)(void*, float);
        reinterpret_cast<Fn>(fn(InitMaxAutoStep))(player, 1.0625f);
    }

    if (on("Fly") && getAbilities && getLayer && setAbility) {
        void* abilities = getAbilities(player);
        void* layer = abilities ? getLayer(abilities, 2) : nullptr;
        if (layer) setAbility(layer, 10, true);
    }

    if (on("KillAura") && getRuntimeActors && attack && isAttackable && getHealth) {
        void* level = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetLevelField);
        void* gameMode = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetGameModeField);
        void* posField = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(player) + GetPositionField);
        if (level && gameMode && posField) {
            const float px = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField));
            const float py = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField) + 4);
            const float pz = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(posField) + 8);
            void* best = nullptr;
            float bestDist2 = 16.0f; // 4 blocks squared
            for (void* actor : getRuntimeActors(level)) {
                if (!actor || actor == player || !isAttackable(actor) || getHealth(actor) <= 0) continue;
                void* targetPos = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
                if (!targetPos) continue;
                const float dx = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)) - px;
                const float dy = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)+4) - py;
                const float dz = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(targetPos)+8) - pz;
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < bestDist2) { bestDist2 = d2; best = actor; }
            }
            if (best) attack(gameMode, best, true);
        }
    }
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

    // Only bind ability helpers when the offset table supplies non-zero values.
    if (GetAbilities) getAbilities = reinterpret_cast<GetAbilitiesFn>(fn(GetAbilities));
    if (GetLayer)     getLayer     = reinterpret_cast<GetLayerFn>(fn(GetLayer));
    if (SetAbility)   setAbility   = reinterpret_cast<SetAbilityFn>(fn(SetAbility));

    getRuntimeActors = reinterpret_cast<GetRuntimeActorFn>(fn(GetRuntimeActor));
    attack           = reinterpret_cast<AttackFn>(fn(Attack));
    isAttackable     = reinterpret_cast<IsAttackableFn>(fn(IsAttackable));
    getHealth        = reinterpret_cast<GetHealthFn>(fn(GetHealth));

    if (DobbyHook(fn(NormalTick), reinterpret_cast<void*>(hookedNormalTick), reinterpret_cast<void**>(&oldNormalTick)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "NormalTick hook failed");
        return false;
    }
    if (DobbyHook(fn(SpeedHack), reinterpret_cast<void*>(hookedSpeed), reinterpret_cast<void**>(&oldSpeed)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Speed hook failed");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "1.21.111 gameplay hooks installed: Speed/Fly/KillAura/Step/Sprint");
    return true;
}
void shutdown() {}
} // namespace eclient_runtime::modules::gameplay
