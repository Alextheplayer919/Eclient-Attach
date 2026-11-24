#pragma once
// Opt-in TEST BUILD ONLY. No automatic inventory mutation or crystal placement.
// Shared UI/game/transfer-hook state is atomic; borrowed game pointers never
// leave the tick/call in which they were supplied.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace eclient_combat_lab {
// 0 idle, 1 armed, 2 expired, 3 attempted, 4 cancelled.
static std::atomic<int> crystalState{0};
static std::atomic<long long> deadlineMs{0};
static std::atomic<bool> observing{false};
static std::atomic<int> events{0}, observerState{0}; // 0 pending, 1 active, -1 unavailable
static std::atomic<int> withContext{0}, withInput{0};
using TransferFn = void (*)(void*,void*,std::uintptr_t);
static TransferFn originalTransfer=nullptr;
static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void transferObserver(void* self,void* input,std::uintptr_t third) {
    if(observing.load(std::memory_order_relaxed)) {
        int n=events.fetch_add(1,std::memory_order_relaxed)+1;
        if(self) withContext.fetch_add(1,std::memory_order_relaxed);
        if(input) withInput.fetch_add(1,std::memory_order_relaxed);
        if(n<=25) __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","transfer candidate event=%d self=%d input=%d third_nonzero=%d (not confirmation)",n,self!=nullptr,input!=nullptr,third!=0);
    }
    originalTransfer(self,input,third);
}
static void install() {
    constexpr std::uintptr_t offset=0xcdc921c;
    const unsigned char expected[]={0xff,0x83,0x01,0xd1,0xfd,0x7b,0x02,0xa9,0xfd,0x83,0x00,0x91,0xf7,0x1b,0x00,0xf9};
    auto address=getAbsoluteAddress(MineLib,offset);
    if(!address || std::memcmp(reinterpret_cast<const void*>(address),expected,sizeof(expected))!=0) {
        observerState.store(-1);__android_log_print(ANDROID_LOG_ERROR,"EClientCombatLab","transfer hook prologue mismatch; not installed");return;
    }
    A64HookFunction(reinterpret_cast<void*>(address),reinterpret_cast<void*>(transferObserver),reinterpret_cast<void**>(&originalTransfer));
    observerState.store(originalTransfer ? 1 : -1);
    __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","transfer hook installed=%d",originalTransfer!=nullptr);
}
static void cancelIfArmed() {
    int expected=1;crystalState.compare_exchange_strong(expected,4);
}
static void tick(LocalPlayer* player,GameMode* game,vec3_t* position,const std::vector<Mob*>& actors) {
    if(crystalState.load(std::memory_order_acquire)!=1) return;
    if(nowMs()>deadlineMs.load(std::memory_order_relaxed)) {int expected=1;crystalState.compare_exchange_strong(expected,2);return;}
    if(!player || !game || !position || !std::isfinite(position->x) || !std::isfinite(position->y) || !std::isfinite(position->z)) return;
    Mob* nearest=nullptr;float best=16.0f;
    for(Mob* actor:actors) {
        if(!actor || actor==player || actor->getEntityTypeId()!=71) continue;
        const vec3_t* p=actor->getPosition();if(!p)continue;
        float x=p->x-position->x,y=p->y-position->y,z=p->z-position->z;
        float d=x*x+y*y+z*z;if(std::isfinite(d) && d<=best){best=d;nearest=actor;}
    }
    if(nearest) {
        int expected=1;
        if(crystalState.compare_exchange_strong(expected,3)) {
            __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","crystal attack attempt within 4 blocks; no success confirmation");
            game->attack(nearest,true);
        }
    }
}
static void settings() {
    ImGui::TextWrapped("EXPERIMENTAL LOCAL-WORLD TEST. Crystal explosions can damage you and the world. No automatic placement or totem switching.");
    if(ImGui::Button("Arm one crystal attack (10s)")) {
        deadlineMs.store(nowMs()+10000,std::memory_order_relaxed);
        crystalState.store(1,std::memory_order_release);
    }
    ImGui::SameLine();if(ImGui::Button("Cancel attack")) cancelIfArmed();
    const char* status="Idle";
    switch(crystalState.load()) {case 1:status="Armed: waiting for crystal within 4 blocks";break;case 2:status="Expired: no crystal found";break;case 3:status="Attack attempted (not proof it broke)";break;case 4:status="Cancelled or world unavailable";break;}
    ImGui::TextWrapped("Crystal test: %s",status);
    ImGui::Separator();
    ImGui::TextWrapped("Transfer observer is passive: manually move items into/out of offhand, then check the count. It does not equip totems.");
    ImGui::Text("Transfer hook: %s",observerState.load()==1 ? "installed" : observerState.load()<0 ? "unavailable" : "pending");
    if(ImGui::Button("Start transfer observation")) {events.store(0);withContext.store(0);withInput.store(0);observing.store(true);}
    ImGui::SameLine();if(ImGui::Button("Stop observation")) observing.store(false);
    ImGui::Text("Events: %d (context %d, input %d)",events.load(),withContext.load(),withInput.load());
    ImGui::TextWrapped("Events are calls, not proof of a successful or server-confirmed inventory move.");
}
}
