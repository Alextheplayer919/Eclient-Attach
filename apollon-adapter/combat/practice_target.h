#pragma once
// Local-only simulated target. Never inserted into Minecraft's actor registry.
#include <atomic>
#include <cmath>
#include <mutex>
#include "practice_state.h"
#include "crystal_break_selection.h"
namespace eclient_practice_target {
struct Snapshot { bool active = false; float x=0, y=0, z=0, health=0; unsigned pops=0; };
static std::mutex mutex;
static eclient::combat::PracticeState practice;
static float x=0, y=0, z=0;
// 1 requests spawn at the player's current position on the game tick, 2 removes.
static std::atomic<int> request{0};
static Snapshot snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return {practice.active, x,y,z,practice.health,practice.pops};
}
static void draw() {
    const auto s = snapshot();
    ImGui::TextWrapped("Local-only simulated target, not a Minecraft actor. Other players cannot see it.");
    ImGui::TextWrapped("Spawns 3 blocks in front of you. Simulated pops occur when the break test sends an attack near it; damage is not calculated.");
    if (ImGui::Button("Spawn / reset target")) request.store(1);
    ImGui::SameLine();
    if (ImGui::Button("Remove target")) request.store(2);
    if (s.active) ImGui::Text("Active | simulated health: %.0f | simulated pops: %u", s.health, s.pops);
    else ImGui::TextUnformatted("No target active. Enter a world before spawning.");
}
static void clear() {
    request.store(0);
    std::lock_guard<std::mutex> lock(mutex);
    eclient::combat::despawn(practice);
}
static void tick(LocalPlayer* player, bool ready) {
    if (!ready || !player || panic || inMenu) { clear(); return; }
    const int action = request.exchange(0);
    if (action == 2) {
        std::lock_guard<std::mutex> lock(mutex);
        eclient::combat::despawn(practice);
    } else if (action == 1) {
        vec3_t* p = player->getPosition();
        vec2_t* rotation = player->getRotation();
        if (!p || !rotation || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z) ||
            !std::isfinite(rotation->y) || std::fabs(p->x)>30000000 || std::fabs(p->z)>30000000 || std::fabs(p->y)>4096) return;
        const float yaw = rotation->y * 0.01745329251994329577f;
        std::lock_guard<std::mutex> lock(mutex);
        x=p->x-3.0f*std::sin(yaw); y=p->y-1.62f; z=p->z+3.0f*std::cos(yaw);
        eclient::combat::spawn(practice,0,true);
    }
}
// This is a deterministic simulation triggered by an attack request, not a
// claim that an explosion happened or actual Bedrock damage was dealt.
static void simulateAttack(float cx, float cy, float cz) {
    std::lock_guard<std::mutex> lock(mutex);
    const float d2=eclient::combat::crystalDistanceSquared(x,y,z,cx,cy,cz);
    if (practice.active && std::isfinite(d2) && d2 <= 16.0f)
        eclient::combat::applyPracticeDamage(practice,20.0f);
}
static void render(ImDrawList* list, vec3_t camera, glm::vec2 fov, glm::mat4 matrix) {
    const auto s = snapshot();
    if (!s.active || !list || screenWidth <= 0 || screenHeight <= 0) return;
    glm::vec2 top, bottom;
    const glm::vec3 origin(camera.x,camera.y,camera.z);
    const glm::vec2 display(screenWidth,screenHeight);
    if (!OWorldToScreen(origin,glm::vec3(s.x,s.y+1.8f,s.z),top,fov,display,matrix) ||
        !OWorldToScreen(origin,glm::vec3(s.x,s.y,s.z),bottom,fov,display,matrix)) return;
    if (!std::isfinite(top.x) || !std::isfinite(top.y) || !std::isfinite(bottom.x) || !std::isfinite(bottom.y)) return;
    const float height = std::fabs(bottom.y-top.y);
    if (height < 2 || height > display.y*2) return;
    const float half = height*0.22f;
    const ImU32 color = IM_COL32(80,230,255,240);
    list->AddRect(ImVec2(top.x-half,top.y),ImVec2(bottom.x+half,bottom.y),color,0,0,2.0f);
    list->AddLine(ImVec2(top.x-half,top.y+height*0.25f),ImVec2(top.x+half,top.y+height*0.25f),color,2.0f);
    list->AddLine(ImVec2(top.x,top.y+height*0.55f),ImVec2(bottom.x,bottom.y),color,2.0f);
    char label[80]; std::snprintf(label,sizeof(label),"Practice | simulated pops: %u",s.pops);
    list->AddText(ImVec2(top.x-half,top.y-ImGui::GetFontSize()-3),color,label);
}
}
