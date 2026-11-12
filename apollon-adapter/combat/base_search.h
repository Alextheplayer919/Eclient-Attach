#pragma once
#include <cmath>
#include "crystal_plan.h"
namespace eclient::combat {
struct Point3 { float x, y, z; };
struct SearchResult { bool found = false; Pos base{}; float distanceSquared = 0; };
// Read-only candidate search. ReadBlock returns unknown for unavailable blocks.
// This checks existing bases only; it does not predict damage or place anything.
template<class ReadBlock>
SearchResult findNearbyBase(Point3 eye, int footY, float reach, ReadBlock readBlock) {
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z) ||
        !std::isfinite(reach) || reach <= 0 || reach > 6 ||
        std::fabs(eye.x) > 30000000 || std::fabs(eye.z) > 30000000 || std::fabs(eye.y) > 4096 ||
        footY < -4096 || footY > 4096) return {};
    SearchResult best;
    const int radius = static_cast<int>(std::ceil(reach));
    const int cx = static_cast<int>(std::floor(eye.x)), cz = static_cast<int>(std::floor(eye.z));
    for (int y = footY-2; y <= footY+1; ++y) {
        for (int x = cx-radius; x <= cx+radius; ++x) for (int z = cz-radius; z <= cz+radius; ++z) {
            const float dx=x+0.5f-eye.x, dy=y+1.0f-eye.y, dz=z+0.5f-eye.z;
            const float d2 = dx*dx+dy*dy+dz*dz;
            if (d2 > reach*reach || (best.found && d2 >= best.distanceSquared)) continue;
            Pos pos{x,y,z}; Block base = readBlock(pos);
            if (base != Block::obsidian && base != Block::bedrock) continue;
            if (readBlock({x,y+1,z}) != Block::air || readBlock({x,y+2,z}) != Block::air) continue;
            best = {true, pos, d2};
        }
    }
    return best;
}
}
