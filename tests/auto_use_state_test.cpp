#include "../apollon-adapter/automation/use_state.h"
#include <cassert>
using namespace eclient_use_state;
int main(){
    Attempt a;a.start(3,1,5,100);assert(a.active);
    assert(a.check(true,true,true,5,1000)==Result::waiting);
    assert(a.check(true,true,true,4,2000)==Result::observed && !a.active);
    a.start(2,1,1,3000);assert(a.check(true,true,false,1,3100)==Result::observed);
    a.start(2,1,1,3000);assert(a.check(true,true,true,1,6499)==Result::waiting);
    assert(a.check(true,true,true,1,6500)==Result::timeout && !a.active);
    a.start(2,1,1,1);assert(a.check(true,false,false,0,2)==Result::cancelled);
    a.start(2,1,1,1);assert(a.check(false,true,true,1,2)==Result::cancelled);
}
