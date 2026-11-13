#pragma once
// Read-only Bedrock .mcstructure model. No Minecraft APIs are called.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eclient::schematic {
struct BlockState {
    enum class Type { Byte, Int, String };
    Type type = Type::Int;
    std::int32_t number = 0;
    std::string text;
};
struct PaletteEntry {
    std::string name;
    std::map<std::string, BlockState> states;
    std::int32_t version = 0;
};
struct Structure {
    int width = 0, height = 0, length = 0;
    std::vector<PaletteEntry> palette;
    // Original Bedrock order: z varies fastest, then y, then x. -1 is void.
    std::vector<std::int32_t> primary, secondary;
    std::int32_t at(int x, int y, int z, bool second = false) const;
};
// Uncompressed little-endian .mcstructure only. Throws std::runtime_error on
// invalid/unsupported inputs. Bounded to 32 MiB and 2 million cells.
Structure readMcStructure(const std::vector<std::uint8_t>& bytes);
}
