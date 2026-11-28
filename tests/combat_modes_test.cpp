#include "../apollon-adapter/combat-lab/combat_modes.h"
#include <cassert>
using namespace eclient_combat_modes;
int main(){
    assert(!shieldLatch(false,9,10,16));assert(shieldLatch(false,16,10,16));
    assert(shieldLatch(true,13,10,16));assert(!shieldLatch(true,10,10,16));
    assert(!shieldLatch(false,10,10,10));
    Cycle c;assert(!c.wantsHit() && c.target==0);
    c.track(123,1000);assert(c.phase==Phase::base && c.target==123);
    c.placeBase(1,2,3,1000);assert(c.phase==Phase::waitBase && !c.wantsHit());
    c.attacked(123,1200);assert(c.phase==Phase::waitBase);
    c.baseReady(1500);assert(c.wantsHit() && !c.ready(1999,500) && c.ready(2000,500));
    c.attacked(99,2000);assert(c.wantsHit());
    c.attacked(123,2000);assert(c.phase==Phase::crystal && !c.ready(2499,500) && c.ready(2500,500));
    for(int i=0;i<3;i++){c.crystalSent(2500+i*1000);assert(c.phase==Phase::waitCrystal);c.crystalFound(2700+i*1000);assert(c.phase==Phase::waitBreak);assert(!c.wantsHit());c.broken(3000+i*1000);}
    assert(c.phase==Phase::base && c.target==0);c.track(1,5000);c.placeBase(1,2,3,5001);c.track(2,5100);assert(c.phase==Phase::base && c.target==2);
    assert(!c.expired(8599,3500));assert(c.expired(8600,3500));
}
