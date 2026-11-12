#include "../apollon-adapter/combat/crystal_plan.h"
#include <cassert>
#include <limits>
using namespace eclient::combat;
int main() {
    Settings c; State state; Site site;
    site.base = {3,64,2}; site.block = Block::air; site.spaceAboveClear = true;
    site.supportFaceAvailable = true; site.targetEligible = true;
    site.hasObsidian = site.hasCrystals = true;
    site.placementDistance = site.crystalDistance = 3; site.targetDistance = 2; site.selfDistance = 4;
    assert(plan(state, 10, site, c).action == Action::none); // baseplate off
    c.baseplate = true;
    auto p = plan(state,10,site,c); assert(p.action == Action::placeBase);
    record(state,10,p,c);
    assert(plan(state,14,site,c).action == Action::none); // wait for block update
    site.block = Block::obsidian;
    p=plan(state,14,site,c); assert(p.action == Action::placeCrystal); record(state,14,p,c);
    assert(plan(state,18,site,c).action == Action::none); // wait for entity
    site.hasCrystal = site.crystalIsAttackable = true;
    p=plan(state,18,site,c); assert(p.action == Action::attackCrystal); record(state,18,p,c);
    assert(plan(state,22,site,c).action == Action::none); // wait for removal
    site.hasCrystal = false; assert(plan(state,22,site,c).action == Action::placeCrystal);
    reset(state); site.block = Block::solid; assert(plan(state,25,site,c).action == Action::none);
    site.block = Block::bedrock; assert(plan(state,25,site,c).action == Action::placeCrystal);
    site.selfDistance = 1; assert(plan(state,25,site,c).action == Action::none);
    site.selfDistance = std::numeric_limits<float>::quiet_NaN(); assert(plan(state,25,site,c).action == Action::none);
    site.selfDistance = 4; site.targetEligible = false; assert(plan(state,25,site,c).action == Action::none);
    site.targetEligible = true; site.hasCrystals = false; assert(plan(state,25,site,c).action == Action::none);
    site.hasCrystals = true; site.spaceAboveClear = false; assert(plan(state,25,site,c).action == Action::none);
    site.spaceAboveClear = true; site.block = Block::air; p=plan(state,30,site,c); record(state,30,p,c);
    assert(plan(state,10,site,c).action == Action::placeBase); // stale tick resets world state
}
