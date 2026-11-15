#include "../apollon-adapter/fly/fly2.h"
#include "../apollon-adapter/fly/fly2_limits.h"
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
    // All six calculated-mode controls affect their intended output.
    reset(s); i = {{0,64,0},0,1,0,false,false}; c = Settings{};
    c.horizontalBps = 10; c.glide = -0.1f;
    assert(plan(s,i,c,out) == Result::primed);
    assert(plan(s,i,c,out) == Result::motion && near(out.z,0.5f) && near(out.y,-0.1f));
    c.horizontalBps = 20; assert(plan(s,i,c,out) == Result::motion && near(out.z,1));
    i.up = true; c.upBps = 10; c.upHorizontalFactor = 0.3f;
    assert(plan(s,i,c,out) == Result::motion && near(out.y,0.5f) && near(out.z,0.3f));
    c.upBps = 20; c.upHorizontalFactor = 0.8f;
    assert(plan(s,i,c,out) == Result::motion && near(out.y,1) && near(out.z,0.8f));
    i.up = false; i.down = true; c.downBps = 10; c.downHorizontalFactor = 0.4f;
    assert(plan(s,i,c,out) == Result::motion && near(out.y,-0.6f) && near(out.z,0.4f));
    c.downBps = 20; c.downHorizontalFactor = 0.7f;
    assert(plan(s,i,c,out) == Result::motion && near(out.y,-1.1f) && near(out.z,0.7f));
    i.forward = std::numeric_limits<float>::quiet_NaN(); assert(plan(s,i,c,out) == Result::invalid);
    i.forward = 0; assert(plan(s,i,c,out) == Result::primed);
    reset(s); assert(plan(s,i,c,out) == Result::primed);
    float x = 3, y = -2, z = 4;
    assert(capVelocity(x,y,z, Limits{20,10,20}));
    assert(near(std::hypot(x,z), 1) && near(y,-1));
    x = 0; y = 0; z = 0; assert(!capVelocity(x,y,z, Limits{})); // hover stays hover
    x = NAN; assert(!capVelocity(x,y,z, Limits{}));
}
