#pragma once
// Partial Crystal Aura: continuous crystal breaking only. Placement is not wired.
#include <atomic>
#include <cmath>
#include <vector>
#include "crystal_break_selection.h"
namespace eclient_crystal_aura {
static std::atomic<bool> enabled{false}, usePlayers{true}, usePractice{true};
static std::atomic<float> breakRange{4.0f}, targetRange{4.0f};
static void draw() {
    bool active = enabled.load();
    if (ImGui::Button(active ? "Disable Crystal Aura" : "Enable Crystal Aura")) enabled.store(!active);
    ImGui::TextWrapped("Partial implementation: continuously breaks crystals near targets. Automatic crystal placement and inventory switching are not implemented yet.");
    ImGui::TextWrapped("Explosions can damage players and the world. Server reach rules still apply; test in a disposable world.");
    bool players=usePlayers.load(), practice=usePractice.load();
    if (ImGui::Checkbox("Target players", &players)) usePlayers.store(players);
    if (ImGui::Checkbox("Target practice entity", &practice)) usePractice.store(practice);
    float reach=breakRange.load(), near=targetRange.load();
    if (ImGui::SliderFloat("Break reach", &reach, 1.0f, 12.0f, "%.1f")) breakRange.store(reach);
    if (ImGui::SliderFloat("Crystal to target", &near, 1.0f, 12.0f, "%.1f")) targetRange.store(near);
}
struct Point { float x,y,z; };
static void tick(LocalPlayer* player, bool ready) {
    static std::uintptr_t lastSimulatedActor=0;
    if (!enabled.load() || !ready || !player || panic || inMenu) { lastSimulatedActor=0; return; }
    Level* level=player->getLevel(); GameMode* game=player->getGameMode(); vec3_t* p=player->getPosition();
    if (!level || !game || !p || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z)) return;
    auto actors=level->getRuntimeActorList();
    std::vector<Point> targets;
    if (usePractice.load()) {
        const auto target=eclient_practice_target::snapshot();
        if (target.active) targets.push_back({target.x,target.y,target.z});
    }
    if (usePlayers.load()) {
        const int playerType=player->getEntityTypeId();
        if (playerType != 0 && playerType != 71) {
            for (auto* actor : actors) {
                if (!actor || actor == player || actor->getEntityTypeId()!=playerType) continue;
                vec3_t* pos=actor->getPosition();
                if (pos && std::isfinite(pos->x) && std::isfinite(pos->y) && std::isfinite(pos->z)) targets.push_back({pos->x,pos->y,pos->z});
            }
        }
    }
    const float reach=breakRange.load(), range=targetRange.load();
    if (!std::isfinite(reach) || !std::isfinite(range) || reach<=0 || range<=0) return;
    Mob* nearest=nullptr; float best=reach*reach; Point hit{};
    for (auto* actor : actors) {
        if (!actor || actor == player || actor->getEntityTypeId()!=71) continue;
        vec3_t* pos=actor->getPosition(); if (!pos) continue;
        const float d2=eclient::combat::crystalDistanceSquared(p->x,p->y,p->z,pos->x,pos->y,pos->z);
        if (!std::isfinite(d2) || d2>best) continue;
        bool near=false;
        for (const auto& target : targets) {
            const float t2=eclient::combat::crystalDistanceSquared(target.x,target.y,target.z,pos->x,pos->y,pos->z);
            if (std::isfinite(t2) && t2<=range*range) { near=true; break; }
        }
        if (near) { nearest=actor; best=d2; hit={pos->x,pos->y,pos->z}; }
    }
    if (!nearest) { lastSimulatedActor=0; return; }
    if (!enabled.load()) return;
    game->attack(nearest,true);
    // Only simulate once for a continuously observed actor, never retain it
    // as a pointer or dereference it on a later tick.
    auto identity=reinterpret_cast<std::uintptr_t>(nearest);
    if (identity != lastSimulatedActor) eclient_practice_target::simulateAttack(hit.x,hit.y,hit.z);
    lastSimulatedActor=identity;
}
}
