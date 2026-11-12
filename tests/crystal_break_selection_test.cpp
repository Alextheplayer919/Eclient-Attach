#include "../apollon-adapter/combat/crystal_break_selection.h"
#include <cassert>
#include <limits>
int main() {
    using namespace eclient::combat;
    const float inf = std::numeric_limits<float>::infinity();
    assert(crystalDistanceSquared(0,0,0,0,0,4) == 16);
    assert(closerCrystal(16, inf));
    assert(!closerCrystal(16.1f, inf));
    assert(!closerCrystal(inf, inf));
    assert(!closerCrystal(-1, inf));
    assert(closerCrystal(4, 9));
    assert(!closerCrystal(9, 4));
    assert(!std::isfinite(crystalDistanceSquared(0,0,0,inf,0,0)));
}
