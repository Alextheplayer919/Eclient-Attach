#pragma once
// One-shot, opt-in test using Apollon's existing game-thread attack wrapper.
// It is not Crystal Aura: no player targeting, placement or inventory switching.
#include <atomic>
#include <chrono>
#include <cmath>
#include "crystal_break_selection.h"
namespace eclient_crystal_break_test {
enum Status { idle, armed, sent, expired, cancelled };
static std::atomic<int> status{idle};
static std::atomic<long long> deadlineNs{0};
static long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void draw() {
    const int current = status.load();
    ImGui::TextWrapped("Experimental one-shot: attacks the nearest end crystal within 4 blocks. No player targeting or placement.");
    ImGui::TextWrapped("Explosion can kill you and damage the world or other players. Use a disposable test world. Native attack is not device-verified.");
    if (current != armed && ImGui::Button("Arm one crystal break (10 sec)")) {
        deadlineNs.store(nowNs() + 10000000000LL);
        status.store(armed);
    } else if (current == armed && ImGui::Button("Cancel break")) {
        int expected = armed; status.compare_exchange_strong(expected, cancelled);
    }
    const int shown = status.load();
    ImGui::TextUnformatted(shown == armed ? "Armed: waiting for a nearby crystal" :
        shown == sent ? "Attack sent once; check the world for the result" :
        shown == expired ? "Timed out without an attack" :
        shown == cancelled ? "Cancelled or world unavailable" : "Not armed");
}
static void tick(LocalPlayer* player, bool ready) {
    if (status.load() != armed) return;
    if (!ready || !player || panic || inMenu) {
        int expected = armed; status.compare_exchange_strong(expected, cancelled); return;
    }
    if (nowNs() >= deadlineNs.load()) {
        int expected = armed; status.compare_exchange_strong(expected, expired); return;
    }
    Level* level = player->getLevel();
    GameMode* game = player->getGameMode();
    vec3_t* p = player->getPosition();
    if (!level || !game || !p || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z)) return;
    Mob* nearest = nullptr;
    float best = std::numeric_limits<float>::infinity();
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || actor == player || actor->getEntityTypeId() != 71) continue;
        vec3_t* pos = actor->getPosition();
        if (!pos) continue;
        const float d2 = eclient::combat::crystalDistanceSquared(p->x,p->y,p->z,pos->x,pos->y,pos->z);
        if (eclient::combat::closerCrystal(d2, best)) { best = d2; nearest = actor; }
    }
    if (!nearest || nowNs() >= deadlineNs.load()) return;
    int expected = armed;
    if (!status.compare_exchange_strong(expected, sent)) return;
    // Uses the same attack wrapper as Apollon's existing aura modules.
    // This is an attempt, not evidence that the server accepted the attack.
    game->attack(nearest, true);
}
}
