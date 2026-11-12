#include "../apollon-adapter/combat/block_names.h"
#include <cassert>
using namespace eclient::combat;
int main() {
    assert(classifyBlockName("tile.obsidian") == KnownBlock::obsidian);
    assert(classifyBlockName("tile.bedrock") == KnownBlock::bedrock);
    assert(classifyBlockName("tile.glowingobsidian") == KnownBlock::unknown);
    assert(classifyBlockName("obsidian") == KnownBlock::unknown);
    assert(classifyBlockName("tile.obsidian.extra") == KnownBlock::unknown);
    assert(classifyBlockName("") == KnownBlock::unknown);
}
