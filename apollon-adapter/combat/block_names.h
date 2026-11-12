#pragma once
#include <string_view>
namespace eclient::combat {
enum class KnownBlock { unknown, obsidian, bedrock };
// Exact matches only. tile.obsidian is used by Apollon's existing Xray hook;
// tile.bedrock follows the same naming convention but requires device testing.
inline KnownBlock classifyBlockName(std::string_view name) {
    if (name == "tile.obsidian") return KnownBlock::obsidian;
    if (name == "tile.bedrock") return KnownBlock::bedrock;
    return KnownBlock::unknown;
}
inline const char* blockLabel(KnownBlock b) {
    switch (b) { case KnownBlock::obsidian: return "obsidian"; case KnownBlock::bedrock: return "bedrock"; default: return "unknown"; }
}
}
