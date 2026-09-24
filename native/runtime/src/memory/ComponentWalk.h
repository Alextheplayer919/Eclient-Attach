#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eclient_runtime::ecs {

// enTT component-storage resolution for Minecraft Bedrock 1.21.111 arm64,
// transcribed 1:1 from the game's own component getters (see ComponentWalk.cpp
// for the disasm evidence). Every pointer hop is guarded through
// NativeMemory::read (validated against /proc/self/maps) so a struct-mismatch
// returns failure instead of crashing the game.

// One pool descriptor (0x20 bytes) from the registry descriptor array.
struct PoolDesc {
    std::uint32_t typeHash{0};   // descriptor + 0x08
    std::uintptr_t pool{0};      // descriptor + 0x10 (pool object pointer)
    std::uintptr_t descAddr{0};  // address of the descriptor itself
};

// Result of resolving one entity's element inside a pool.
struct ElementInfo {
    bool valid{false};
    std::uintptr_t address{0};   // element base = block + slot*stride (stride is caller-chosen)
    std::uint32_t packed{0};     // page[slot] raw
    std::uint64_t elemIdx{0};    // (packed >> 4) & 0x3FFF
    std::uint64_t inBlock{0};    // (packed & 0x3FFFF) & 0x7F
    std::uintptr_t block{0};     // instances[elemIdx & 0x3FF8]
};

// Dump every pool descriptor in the actor's registry. Bounded to 8192 entries.
std::vector<PoolDesc> listPools(std::uintptr_t actor);

// Resolve entity `eid`'s element inside `pool`. Follows the exact
// page/packed/element-index chain from the GetHealth implementation. The
// returned block pointer is where element data for that entity's block lives;
// the caller picks a stride guess and reads at block + inBlock*stride.
ElementInfo resolveElement(std::uintptr_t pool, std::uint32_t eid);

// Bounds-checked read helper for the dump (returns false when unreadable).
bool readElementFloats(std::uintptr_t address, float* out, int count);
bool readElementWords(std::uintptr_t address, std::uint32_t* out, int count);

// Debug-string helper for logcat dumps.
std::string describe(const ElementInfo& info);

} // namespace eclient_runtime::ecs
