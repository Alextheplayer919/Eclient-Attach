#pragma once
// Crystal breaking and the in-game event console, for the opt-in build.
#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>
#include "live_console.h"
namespace eclient_combat_lab {
static std::atomic<bool> breakAura{false};
static std::atomic<int> breakDelay{500};
static std::atomic<float> breakRange{5.0f};
static long long lastAttack=0;
static long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void cancelIfArmed() {lastAttack=0;}
static void tick(LocalPlayer* player,GameMode* game,vec3_t* position,const std::vector<Mob*>& actors) {
    if(!breakAura.load() || !player || !game || !position || nowMs()-lastAttack<breakDelay.load())return;
    Mob* nearest=nullptr;float range=breakRange.load(),best=range*range;
    for(Mob* actor:actors) {
        if(!actor || actor==player || actor->getEntityTypeId()!=71)continue;
        const vec3_t* p=actor->getPosition();if(!p)continue;
        float x=p->x-position->x,y=p->y-position->y,z=p->z-position->z,d=x*x+y*y+z*z;
        if(std::isfinite(d)&&d<=best){best=d;nearest=actor;}
    }
    if(nearest){lastAttack=nowMs();eclient_console::log.add("Crystal Aura: attack at %.1f blocks",std::sqrt(best));game->attack(nearest,true);}
}
static void install() {eclient_console::log.add("Combat modules ready");}
static std::uint64_t placementExperiment(eclient::compat::PlacementFn original,const eclient::compat::PlacementArgs& args) {
    return eclient::compat::forwardPlacement(original,args);
}
static void drawConsole() {
    if(ImGui::Button("Clear console")) eclient_console::log.clear();
    ImGui::TextWrapped("Recent instrumented events (not every game event).");
    unsigned long long generation=0;
    for(const auto& line:eclient_console::log.snapshot(generation)) ImGui::TextWrapped("%s",line.c_str());
}
}
