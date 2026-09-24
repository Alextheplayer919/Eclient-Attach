#pragma once

#include <cstdint>

namespace eclient_runtime::modules::melody {

// Melody client module ports (KillAura2, velocity Fly) + the ECSDump
// diagnostic module. Bound once the runtime located libminecraftpe.so; runs
// from the LocalPlayer::normalTick hook on the game thread.
bool initialize(std::uintptr_t minecraftBase);
void onNormalTick(void* localPlayer);

} // namespace eclient_runtime::modules::melody
