#include "../apollon-adapter/combat/base_search.h"
#include <cassert>
#include <limits>
using namespace eclient::combat;
int main() {
    auto block = [](Pos p) {
        if (p == Pos{2,63,0} || p == Pos{5,63,0}) return Block::obsidian;
        if (p == Pos{1,63,1}) return Block::bedrock;
        if (p == Pos{1,64,1}) return Block::solid; // no space above bedrock
        return Block::air;
    };
    auto result = findNearbyBase({0.5f,65.62f,0.5f},64,4.5f,block);
    assert(result.found && result.base == (Pos{2,63,0})); // not under player
    assert(!findNearbyBase({0.5f,65.62f,0.5f},64,1.0f,block).found);
    assert(!findNearbyBase({0.5f,65.62f,0.5f},64,7.0f,block).found);
    assert(!findNearbyBase({std::numeric_limits<float>::quiet_NaN(),65,0},64,4,block).found);
    auto unknown = [](Pos p) { return p == Pos{2,63,0} ? Block::obsidian : Block::unknown; };
    assert(!findNearbyBase({0.5f,65.62f,0.5f},64,4.5f,unknown).found); // don't assume unknown is air
}
