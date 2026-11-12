#include "../apollon-adapter/combat/practice_state.h"
#include <cassert>
#include <limits>
using namespace eclient::combat;
int main() {
    PracticeState s;
    assert(applyPracticeDamage(s,100) == PracticeHit::ignored);
    spawn(s,1,false); assert(s.active && s.health == 20 && s.pops == 0);
    assert(applyPracticeDamage(s,3) == PracticeHit::damaged && s.health == 17);
    assert(applyPracticeDamage(s,std::numeric_limits<float>::quiet_NaN()) == PracticeHit::ignored);
    assert(applyPracticeDamage(s,20) == PracticeHit::popped && s.pops == 1 && s.totems == 0 && s.health == 1);
    assert(applyPracticeDamage(s,1) == PracticeHit::defeated && !s.active);
    spawn(s,0,true); assert(applyPracticeDamage(s,100) == PracticeHit::popped && s.active);
    despawn(s); assert(!s.active);
}
