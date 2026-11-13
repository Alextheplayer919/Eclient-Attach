#pragma once
#include <array>
#include <string>
#include <vector>
#include "json.h" // nlohmann JSON 3.11.2 supplied by the Apollon source archive

namespace eclient::schematic {
struct TextureResult {
    std::string logicalPath; // relative to resource pack root, with no extension
    bool variantFallback = false; // first element chosen from a variant array
};
class TextureCatalog {
    nlohmann::json blocks_ = nlohmann::json::object();
    nlohmann::json textures_ = nlohmann::json::object();
public:
    // Accepts JSON with comments. Call in explicit low -> high priority order.
    // Top-level entries override per block/texture key, not per nested property.
    // Exact Minecraft pack merge semantics and active pack order are unverified.
    void addPack(const std::string& blocksJson, const std::string& terrainJson);
    // Direction is up/down/north/south/east/west. States, tint and rotation
    // aren't applied. An empty result means unsupported or not found.
    TextureResult lookup(const std::string& blockId, const std::string& face) const;
};
}
