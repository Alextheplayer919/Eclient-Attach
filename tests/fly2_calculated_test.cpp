#include "../apollon-adapter/fly/fly2_calculated.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace eclient::fly2;
int main() {
    CalculationState s; Settings settings;
    NativeSnapshot input{1, false, {{5,64,5},0,1,0,false,false}};
    Vec3 out{77,88,99};
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::unavailable && out.x == 77);
    input.layoutVerified = true;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::motion);
    assert(std::fabs(out.z-2.3f) < 0.0001f && std::fabs(out.y+0.02f) < 0.0001f);
    input.input.up = true;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::motion);
    assert(std::fabs(out.z-2.3f*.55f) < 0.0001f && std::fabs(out.y-.99f) < 0.0001f);
    input.playerIdentity = 2;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    input.input.position.x += 5;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::positionJump);
    input.input.forward = 10;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::invalid);
    input.input.forward = 1;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    input.input.forward = std::numeric_limits<float>::quiet_NaN();
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::invalid);
    input.input.forward = 1;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    assert(calculate(s,true,false,input,settings,out) == CalculationStatus::unavailable);
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    assert(calculate(s,false,true,input,settings,out) == CalculationStatus::unavailable);
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
    input.layoutVerified = false;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::unavailable);
    input.layoutVerified = true;
    assert(calculate(s,true,true,input,settings,out) == CalculationStatus::primed);
}
