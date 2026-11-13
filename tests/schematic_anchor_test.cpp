#include "../apollon-adapter/schematic/anchor.h"
#include <cassert>
int main() {
    eclient::schematic::Anchor anchor;
    assert(anchor.set({100,64,-30})); assert(anchor.rotate(1));
    auto p=anchor.world(2,3,4); assert(p.x==96 && p.y==67 && p.z==-28);
    anchor.lock(true); assert(!anchor.set({0,0,0}) && !anchor.rotate(2));
    p=anchor.world(2,3,4); assert(p.x==96 && p.y==67 && p.z==-28);
    anchor.lock(false); assert(anchor.rotate(2)); p=anchor.world(2,3,4); assert(p.x==98 && p.y==67 && p.z==-34);
    assert(anchor.rotate(3)); p=anchor.world(2,3,4); assert(p.x==104 && p.z==-32);
    assert(!anchor.set({30000001,0,0})); assert(anchor.position().x==100);
}
