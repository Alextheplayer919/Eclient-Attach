#pragma once
#include <cstdint>
namespace eclient::compat {
// Preserve all seven observed general-purpose ARM64 arguments and the full
// return register. Semantics are deliberately not assumed by the forwarder.
using PlacementFn = std::uint64_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                      std::uint32_t, std::uintptr_t, std::uintptr_t, std::uint32_t);
struct PlacementArgs { std::uintptr_t a0, a1, a2; std::uint32_t a3; std::uintptr_t a4, a5; std::uint32_t a6; };
inline std::uint64_t forwardPlacement(PlacementFn original, const PlacementArgs& a) {
    return original(a.a0,a.a1,a.a2,a.a3,a.a4,a.a5,a.a6);
}
}
