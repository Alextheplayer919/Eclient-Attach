#pragma once
// Opt-in TEST BUILD ONLY. Experiments may mutate the world or inventory.
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
static std::atomic<bool> breakAura{false};
static std::atomic<long long> lastAttack{0};
// One deliberately synthetic placement while the game's own arguments are live.
static std::atomic<int> placeState{0}, placeDirection{0};
static std::atomic<long long> placeDeadline{0};
static std::atomic<unsigned long long> placeResult{0};
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
    bool armed=crystalState.load(std::memory_order_acquire)==1;
    if(armed && nowMs()>deadlineMs.load(std::memory_order_relaxed)) {int expected=1;crystalState.compare_exchange_strong(expected,2);armed=false;}
    const bool aura=breakAura.load(std::memory_order_relaxed) && nowMs()-lastAttack.load(std::memory_order_relaxed)>=500;
    if(!armed && !aura) return;
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
        if((armed && crystalState.compare_exchange_strong(expected,3)) || aura) {
            lastAttack.store(nowMs(),std::memory_order_relaxed);
            __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","crystal attack attempt within 4 blocks; no success confirmation");
            game->attack(nearest,true);
        }
    }
}
// a2 is inferred as the BlockPos pointer (three signed 32-bit coordinates).
// Reuse all other arguments only during the intercepted call. This can fail,
// consume/place the wrong item, desync, or crash.
static std::uint64_t placementExperiment(eclient::compat::PlacementFn original,
                                          const eclient::compat::PlacementArgs& args) {
    auto result=eclient::compat::forwardPlacement(original,args);
    if(placeState.load(std::memory_order_acquire)!=1) return result;
    if(nowMs()>placeDeadline.load(std::memory_order_relaxed)) {int expected=1;placeState.compare_exchange_strong(expected,2);return result;}
    if(!args.a2 || panic) return result;
    int expected=1;
    if(!placeState.compare_exchange_strong(expected,3)) return result;
    struct Pos {std::int32_t x,y,z;};
    Pos pos;
    std::memcpy(&pos,reinterpret_cast<const void*>(args.a2),sizeof(pos));
    int direction=placeDirection.load();
    // Avoid signed overflow when testing corrupted or unexpected position data.
    if(pos.x < -30000000 || pos.x > 30000000 || pos.z < -30000000 || pos.z > 30000000) {placeState.store(4);return result;}
    if(direction==0) ++pos.x; else if(direction==1) --pos.x; else if(direction==2) ++pos.z; else --pos.z;
    auto synthetic=args;synthetic.a2=reinterpret_cast<std::uintptr_t>(&pos);
    __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","attempting synthetic neighboring useItemOn at %d %d %d",pos.x,pos.y,pos.z);
    placeResult.store(eclient::compat::forwardPlacement(original,synthetic));
    return result;
}
static void settings() {
    ImGui::TextWrapped("EXPERIMENTAL LOCAL-WORLD TEST. Can crash, destroy blocks or lose items. AutoTotem is not yet implemented.");
    bool aura=breakAura.load();if(ImGui::Checkbox("Continuous crystal break", &aura)) breakAura.store(aura);
    ImGui::TextWrapped("Attacks nearby crystals every half second. No player targeting or damage calculation.");
    if(ImGui::Button("Arm one crystal attack (10s)")) {
        deadlineMs.store(nowMs()+10000,std::memory_order_relaxed);
        crystalState.store(1,std::memory_order_release);
    }
    ImGui::SameLine();if(ImGui::Button("Cancel attack")) cancelIfArmed();
    const char* status="Idle";
    switch(crystalState.load()) {case 1:status="Armed: waiting for crystal within 4 blocks";break;case 2:status="Expired: no crystal found";break;case 3:status="Attack attempted (not proof it broke)";break;case 4:status="Cancelled or world unavailable";break;}
    ImGui::TextWrapped("Crystal test: %s",status);
    ImGui::Separator();
    int direction=placeDirection.load();
    if(ImGui::Combo("Extra placement direction", &direction, "East +X\0West -X\0South +Z\0North -Z\0")) placeDirection.store(direction);
    if(ImGui::Button("Arm adjacent placement (10s)")) {placeDeadline.store(nowMs()+10000);placeState.store(1);}
    ImGui::SameLine();if(ImGui::Button("Cancel placement")) {int expected=1;placeState.compare_exchange_strong(expected,4);}
    const char* place=placeState.load()==1 ? "Armed: manually use an end crystal on obsidian now" : placeState.load()==2 ? "Expired" : placeState.load()==3 ? "Synthetic call attempted" : placeState.load()==4 ? "Cancelled/invalid position" : "Idle";
    ImGui::TextWrapped("Placement: %s; return=%llu",place,placeResult.load());
    ImGui::TextWrapped("Makes an extra useItemOn call on the adjacent block using the item you manually used. It may place any held item, not just crystals. Build an obsidian platform first.");
    ImGui::Separator();
    ImGui::TextWrapped("Transfer observer is passive: manually move items into/out of offhand, then check the count. It does not equip totems.");
    ImGui::Text("Transfer hook: %s",observerState.load()==1 ? "installed" : observerState.load()<0 ? "unavailable" : "pending");
    if(ImGui::Button("Start transfer observation")) {events.store(0);withContext.store(0);withInput.store(0);observing.store(true);}
    ImGui::SameLine();if(ImGui::Button("Stop observation")) observing.store(false);
    ImGui::Text("Events: %d (context %d, input %d)",events.load(),withContext.load(),withInput.load());
    ImGui::TextWrapped("Events are calls, not proof of a successful or server-confirmed inventory move.");
}
}
