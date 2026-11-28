#include "../apollon-adapter/combat-lab/fake_simulation.h"
#include <cassert>
#include <cmath>
using namespace eclient_fake_sim;
int main(){
    State s;Settings cfg;cfg.armor=0;cfg.gapples=false;cfg.totems=1;cfg.noKnockback=true;
    s.reset({0,1,0},cfg);assert(s.health==20 && s.totems==1);
    assert(s.damage(25,{1,1,0},cfg)==2);assert(s.health==1 && s.totems==0 && s.absorption==8);
    assert(s.damage(25,{1,1,0},cfg)==0);for(int i=0;i<10;i++)s.healthTick(cfg);
    assert(s.damage(25,{1,1,0},cfg)==3 && s.dead==1);
    s.reset({0,1,0},cfg);cfg.armor=2;cfg.protection=4;assert(s.reduce(20,cfg)<20);
    cfg.armor=0;int r=s.explosion({1,1,0},cfg,[](float,float,float){return false;});assert(r==2);
    s.reset({0,1,0},cfg);cfg.move=true;
    for(int i=0;i<40;i++)s.physics(cfg,[](float,float y,float){return y<1;});
    assert(std::isfinite(s.feet.x) && s.feet.x>0.1f && s.feet.y>=1.f);
    s.throwPearl({8,2,0});assert(s.pearlActive);for(int i=0;i<250 && s.pearlActive;i++)s.pearlTick([](float,float y,float){return y<1;});assert(!s.pearlActive);
}
