#include "../apollon-adapter/fly/fly2.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace eclient::fly2;
static bool near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
int main() {
    State s; Settings c; Vec3 out{123, 456, 789};
    Input i{{100, 64, -50}, 0, 1, 0, false, false};
    assert(plan(s,i,c,out) == Result::primed && out.x == 123);
    assert(plan(s,i,c,out) == Result::motion && near(out.x,0) && near(out.y,-.02f) && near(out.z,2.3f));
    i.yawDegrees = 90; assert(plan(s,i,c,out) == Result::motion && near(out.x,-2.3f) && near(out.z,0));
    i.up = true; i.down = true;
    assert(plan(s,i,c,out) == Result::motion && near(out.x,-2.3f*.55f*.55f) && near(out.y,.99f));
    i.up = false; assert(plan(s,i,c,out) == Result::motion && near(out.y,-2.32f));
    i.position.x += 5; assert(plan(s,i,c,out) == Result::positionJump);
    i.down = false; i.forward = 0; c.glide = 0; assert(plan(s,i,c,out) == Result::idle);
    i.forward = 1; i.strafe = 1; i.yawDegrees = 0; c.horizontalBps = 100;
    assert(plan(s,i,c,out) == Result::motion && near(std::hypot(out.x,out.z),std::sqrt(5.99f)));
    i.forward = std::numeric_limits<float>::quiet_NaN(); assert(plan(s,i,c,out) == Result::invalid);
    i.forward = 0; assert(plan(s,i,c,out) == Result::primed);
    reset(s); assert(plan(s,i,c,out) == Result::primed);
}
