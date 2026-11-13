#pragma once
// Standalone provisional full-cube mesh. No game hooks, image loading or GPU calls.
#include "anchor.h"
#include "mcstructure.h"
#include "texture_catalog.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace eclient::schematic {
struct MeshVertex { double x, y, z; float u, v; };
struct MeshFace {
    std::array<MeshVertex,4> vertices;
    std::string texture; // logical resource path; image existence is checked by caller
    bool variantFallback;
};
struct CubeMesh {
    std::vector<MeshFace> faces;
    std::size_t skippedBlocks = 0;
    std::size_t statefulBlocks = 0;
    std::size_t secondaryBlocks = 0;
    std::size_t missingTextures = 0;
};
// Only explicitly supported provisional cubes: stone, dirt, grass. Stateful
// blocks and texture variants are reported as approximations. No transparency,
// tint, models, animations, active packs or secondary layer drawing.
// Throws on invalid dimensions/layers/indices or when faceLimit is exceeded.
CubeMesh buildCubeMesh(const Structure&, const Anchor&, const TextureCatalog&,
                       std::size_t faceLimit = 120000);
}
