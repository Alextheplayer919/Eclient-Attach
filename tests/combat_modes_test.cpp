#include "../apollon-adapter/combat-lab/combat_modes.h"
#include <cassert>
using namespace eclient_combat_modes;
int main(){
    assert(!shieldLatch(false,9,10,16));assert(shieldLatch(false,16,10,16));
    assert(shieldLatch(true,13,10,16));assert(!shieldLatch(true,10,10,16));
    assert(!shieldLatch(false,10,10,10));
    Cycle c;assert(c.wantsHit());c.attacked(123,1000);assert(!c.wantsHit());
    assert(!c.ready(1499,500));assert(c.ready(1500,500));
    c.placeBase(1,2,3,1500);assert(c.phase==Phase::waitBase && !c.ready(1999,500));
    c.baseReady(1800);assert(!c.ready(1999,500) && c.ready(2000,500));
    for(int i=0;i<3;i++){c.crystalSent(2000+i*1000);assert(c.phase==Phase::waitCrystal);c.crystalFound(2200+i*1000);assert(c.phase==Phase::waitBreak);assert(!c.wantsHit());c.broken(2500+i*1000);}
    assert(c.wantsHit() && c.target==0);c.attacked(1,5000);c.track(2);assert(c.wantsHit());
    c.attacked(3,6000);assert(!c.expired(9499,3500));assert(c.expired(9500,3500));
}
