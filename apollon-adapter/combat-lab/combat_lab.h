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
#include "live_console.h"
namespace eclient_combat_lab {
// 0 idle, 1 armed, 2 expired, 3 attempted, 4 cancelled.
static std::atomic<int> crystalState{0};
static std::atomic<long long> deadlineMs{0};
static std::atomic<bool> breakAura{false};
static std::atomic<long long> lastAttack{0};
static std::atomic<int> attackAttempts{0};
// One deliberately synthetic placement while the game's own arguments are live.
static std::atomic<int> placeState{0}, placeDirection{0}, placeOrder{0}, placeMethod{0};
static std::atomic<long long> placeDeadline{0};
static std::atomic<unsigned long long> placeResult{0};
static std::atomic<int> events{0}, observerState{0}; // 0 pending, 1 active, -1 unavailable
static std::atomic<int> withContext{0}, withInput{0};
// When armed, deliberately invoke the real transfer entry a second time while
// all arguments from a manual inventory interaction are still in scope.
static std::atomic<int> transferReplay{0};
static std::atomic<long long> transferDeadline{0};
using TransferFn = void (*)(void*,void*,std::uintptr_t);
static TransferFn originalTransfer=nullptr;
// Slot 123 candidate takes three leading arguments followed by a 12-byte
// BlockPos passed in x3/x4. The remaining registers are forwarded untouched.
using CrystalItemFn = std::uint64_t (*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint64_t,std::uint32_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
static CrystalItemFn originalCrystalItem=nullptr;
static std::atomic<int> crystalHookState{0};
static std::uint64_t crystalItemHook(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint64_t,std::uint32_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void transferObserver(void* self,void* input,std::uintptr_t third) {
    int n=events.fetch_add(1,std::memory_order_relaxed)+1;
    if(self) withContext.fetch_add(1,std::memory_order_relaxed);
    if(input) withInput.fetch_add(1,std::memory_order_relaxed);
    eclient_console::log.add("Transfer event %d: context=%s input=%s third=%s",n,self?"yes":"no",input?"yes":"no",third?"yes":"no");
    if(transferReplay.load(std::memory_order_acquire)==1) {
        int expected=1;
        if(nowMs()>transferDeadline.load(std::memory_order_relaxed)) {if(transferReplay.compare_exchange_strong(expected,2)) eclient_console::log.add("Extra transfer expired");}
        else if(self && input && third && transferReplay.compare_exchange_strong(expected,3)) {
            eclient_console::log.add("Extra inventory transfer: calling native function (not success confirmation)");
            __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","calling extra native transfer with live arguments (attempt only)");
            originalTransfer(self,input,third);
        }
    }
    originalTransfer(self,input,third);
}
static void install() {
    constexpr std::uintptr_t crystalOffset=0x5885518;
    const unsigned char crystalExpected[]={0xfd,0x7b,0xbb,0xa9,0xfc,0x67,0x01,0xa9,0xfd,0x03,0x00,0x91,0xf8,0x5f,0x02,0xa9};
    auto crystalAddress=getAbsoluteAddress(MineLib,crystalOffset);
    if(crystalAddress && std::memcmp(reinterpret_cast<const void*>(crystalAddress),crystalExpected,sizeof(crystalExpected))==0) {
        A64HookFunction(reinterpret_cast<void*>(crystalAddress),reinterpret_cast<void*>(crystalItemHook),reinterpret_cast<void**>(&originalCrystalItem));
        crystalHookState.store(originalCrystalItem ? 1 : -1);
    } else crystalHookState.store(-1);
    eclient_console::log.add("Crystal item slot 123 hook: %s",crystalHookState.load()==1 ? "installed" : "unavailable");
    constexpr std::uintptr_t offset=0xcdc921c;
    const unsigned char expected[]={0xff,0x83,0x01,0xd1,0xfd,0x7b,0x02,0xa9,0xfd,0x83,0x00,0x91,0xf7,0x1b,0x00,0xf9};
    auto address=getAbsoluteAddress(MineLib,offset);
    if(!address || std::memcmp(reinterpret_cast<const void*>(address),expected,sizeof(expected))!=0) {
        observerState.store(-1);eclient_console::log.add("Inventory hook unavailable: target prologue mismatch");__android_log_print(ANDROID_LOG_ERROR,"EClientCombatLab","transfer hook prologue mismatch; not installed");return;
    }
    A64HookFunction(reinterpret_cast<void*>(address),reinterpret_cast<void*>(transferObserver),reinterpret_cast<void**>(&originalTransfer));
    observerState.store(originalTransfer ? 1 : -1);
    eclient_console::log.add("Inventory transfer hook: %s",originalTransfer ? "installed" : "failed");
    __android_log_print(ANDROID_LOG_INFO,"EClientCombatLab","transfer hook installed=%d",originalTransfer!=nullptr);
}
static void cancelIfArmed() {
    int expected=1;if(crystalState.compare_exchange_strong(expected,4)) eclient_console::log.add("Crystal attack cancelled");
}
static void tick(LocalPlayer* player,GameMode* game,vec3_t* position,const std::vector<Mob*>& actors) {
    bool armed=crystalState.load(std::memory_order_acquire)==1;
    if(armed && nowMs()>deadlineMs.load(std::memory_order_relaxed)) {int expected=1;if(crystalState.compare_exchange_strong(expected,2)) eclient_console::log.add("Crystal attack expired: no crystal found");armed=false;}
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
            int attacks=attackAttempts.fetch_add(1)+1;
            eclient_console::log.add("Crystal attack #%d attempted (distance %.1f blocks)",attacks,std::sqrt(best));
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
    if(placeMethod.load()!=0 || placeState.load(std::memory_order_acquire)!=1) {
        eclient_console::log.add("GameMode use event: face %u",args.a3);
        auto result=eclient::compat::forwardPlacement(original,args);
        eclient_console::log.add("GameMode use returned %llu",static_cast<unsigned long long>(result));
        return result;
    }
    if(nowMs()>placeDeadline.load(std::memory_order_relaxed)) {
        int expected=1;
        if(placeState.compare_exchange_strong(expected,2)) eclient_console::log.add("Placement expired before use hook");
        return eclient::compat::forwardPlacement(original,args);
    }
    if(!args.a2 || panic) {
        eclient_console::log.add("Placement skipped: %s",panic ? "panic enabled" : "position argument null");
        return eclient::compat::forwardPlacement(original,args);
    }
    int expected=1;
    if(!placeState.compare_exchange_strong(expected,3)) return eclient::compat::forwardPlacement(original,args);
    struct Pos {std::int32_t x,y,z;};
    Pos pos;
    std::memcpy(&pos,reinterpret_cast<const void*>(args.a2),sizeof(pos));
    if(pos.x < -30000000 || pos.x > 30000000 || pos.z < -30000000 || pos.z > 30000000) {
        placeState.store(4);eclient_console::log.add("Placement cancelled: invalid position");
        return eclient::compat::forwardPlacement(original,args);
    }
    eclient_console::log.add("Manual target %d %d %d, face %u",pos.x,pos.y,pos.z,args.a3);
    int direction=placeDirection.load();
    if(direction==0) ++pos.x; else if(direction==1) --pos.x; else if(direction==2) ++pos.z; else --pos.z;
    auto synthetic=args;synthetic.a2=reinterpret_cast<std::uintptr_t>(&pos);
    // Ghidra validation shows x4 points to an absolute Vec3 hit location.
    // Move the hit coordinate together with the block position.
    float hit[3];
    if(args.a4) {
        std::memcpy(hit,reinterpret_cast<const void*>(args.a4),sizeof(hit));
        if(direction==0) hit[0]+=1;else if(direction==1) hit[0]-=1;else if(direction==2) hit[2]+=1;else hit[2]-=1;
        synthetic.a4=reinterpret_cast<std::uintptr_t>(hit);
        eclient_console::log.add("Adjusted GameMode hit point for adjacent block");
    } else eclient_console::log.add("GameMode hit point null; forwarded unchanged");
    const bool before=placeOrder.load()==0;
    auto extraCall=[&]() {
        eclient_console::log.add("Trying adjacent %d %d %d (%s manual)",pos.x,pos.y,pos.z,before ? "before" : "after");
        auto extra=eclient::compat::forwardPlacement(original,synthetic);
        placeResult.store(extra);
        eclient_console::log.add("Adjacent native return %llu; check world",static_cast<unsigned long long>(extra));
    };
    if(before) extraCall();
    eclient_console::log.add("Forwarding manual use");
    auto result=eclient::compat::forwardPlacement(original,args);
    eclient_console::log.add("Manual native return %llu; check world",static_cast<unsigned long long>(result));
    if(!before) extraCall();
    return result;
}
static std::uint64_t crystalItemHook(std::uintptr_t a0,std::uintptr_t a1,std::uintptr_t a2,
        std::uint64_t xy,std::uint32_t z,std::uintptr_t a5,std::uintptr_t a6,std::uintptr_t a7) {
    if(placeMethod.load()!=1 || placeState.load(std::memory_order_acquire)!=1) {
        eclient_console::log.add("EndCrystalItem use event");
        auto result=originalCrystalItem(a0,a1,a2,xy,z,a5,a6,a7);
        eclient_console::log.add("EndCrystalItem returned %llu",static_cast<unsigned long long>(result));
        return result;
    }
    if(nowMs()>placeDeadline.load()) {
        int expected=1;if(placeState.compare_exchange_strong(expected,2)) eclient_console::log.add("Crystal item test expired");
        return originalCrystalItem(a0,a1,a2,xy,z,a5,a6,a7);
    }
    if(panic) return originalCrystalItem(a0,a1,a2,xy,z,a5,a6,a7);
    int expected=1;
    if(!placeState.compare_exchange_strong(expected,3)) return originalCrystalItem(a0,a1,a2,xy,z,a5,a6,a7);
    std::uint32_t ux=static_cast<std::uint32_t>(xy),uy=static_cast<std::uint32_t>(xy>>32);
    std::int32_t x,y,zi;
    std::memcpy(&x,&ux,4);std::memcpy(&y,&uy,4);std::memcpy(&zi,&z,4);
    if(x < -30000000 || x > 30000000 || zi < -30000000 || zi > 30000000) {
        placeState.store(4);eclient_console::log.add("Crystal item test: invalid position");
        return originalCrystalItem(a0,a1,a2,xy,z,a5,a6,a7);
    }
    eclient_console::log.add("Crystal item manual target %d %d %d",x,y,zi);
    int dir=placeDirection.load();
    if(dir==0)++x;else if(dir==1)--x;else if(dir==2)++zi;else --zi;
    std::memcpy(&ux,&x,4);std::memcpy(&z,&zi,4);
    std::uint64_t adjacent=static_cast<std::uint64_t>(ux) | (static_cast<std::uint64_t>(uy)<<32);
    const bool before=placeOrder.load()==0;
    auto extraCall=[&]() {
        eclient_console::log.add("Crystal item slot 123 call at %d %d %d (%s manual)",x,y,zi,before ? "before" : "after");
        auto extra=originalCrystalItem(a0,a1,a2,adjacent,z,a5,a6,a7);
        placeResult.store(extra);
        eclient_console::log.add("Crystal item returned %llu; check world",static_cast<unsigned long long>(extra));
    };
    if(before) extraCall();
    // z was shifted for the extra call, so reconstruct the unmodified manual z.
    std::int32_t originalZ=zi;
    if(dir==2)--originalZ;else if(dir==3)++originalZ;
    std::uint32_t originalBits;std::memcpy(&originalBits,&originalZ,4);
    auto result=originalCrystalItem(a0,a1,a2,xy,originalBits,a5,a6,a7);
    eclient_console::log.add("Crystal item manual return %llu",static_cast<unsigned long long>(result));
    if(!before) extraCall();
    return result;
}
static void placementControls() {
    int state=placeState.load();
    if(state==1 && nowMs()>placeDeadline.load()) {
        int expected=1;
        if(placeState.compare_exchange_strong(expected,2)) eclient_console::log.add("Placement expired: selected hook not reached in 30 seconds");
        state=placeState.load();
    }
    int method=placeMethod.load();
    if(ImGui::Combo("Placement method", &method, "GameMode + adjusted hit\0EndCrystalItem slot 123\0")) placeMethod.store(method);
    ImGui::Text("Crystal item hook: %s",crystalHookState.load()==1 ? "installed" : crystalHookState.load()<0 ? "unavailable" : "pending");
    int direction=placeDirection.load();
    if(ImGui::Combo("Adjacent block", &direction, "East +X\0West -X\0South +Z\0North -Z\0")) placeDirection.store(direction);
    int order=placeOrder.load();
    if(ImGui::Combo("Extra call order", &order, "Before manual (new test)\0After manual (old test)\0")) placeOrder.store(order);
    if(ImGui::Button("Arm placement test (30s)")) {
        placeResult.store(0);placeDeadline.store(nowMs()+30000);placeState.store(1);
        eclient_console::log.add("Placement armed: %s. Use crystal on first obsidian block.",placeMethod.load()==0 ? "GameMode + adjusted hit" : "EndCrystalItem slot 123");
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel placement")) {
        int expected=1;
        if(placeState.compare_exchange_strong(expected,4)) eclient_console::log.add("Placement cancelled");
    }
    state=placeState.load();
    if(state==1) ImGui::Text("Placement armed: %lld seconds left",(placeDeadline.load()-nowMs()+999)/1000);
    else ImGui::TextWrapped("Placement: %s; return=%llu",state==2 ? "expired" : state==3 ? "native call attempted" : state==4 ? "cancelled/invalid position" : "idle",placeResult.load());
    ImGui::TextWrapped("Place adjacent obsidian; select direction, arm, then use a held crystal on the first block. Check both blocks and console. Other held items can be used too.");
}
static void drawConsole() {
    ImGui::TextWrapped("Live console: captured combat hooks and some module changes (newest 100, memory only). Not all game events are hooked.");
    if(ImGui::Button("Clear console")) {eclient_console::log.clear();}
    ImGui::SameLine();
    static bool follow=true;ImGui::Checkbox("Follow latest",&follow);
    ImGui::Text("Transfer hook: %s | Crystal item hook: %s",observerState.load()==1 ? "installed" : observerState.load()<0 ? "unavailable" : "pending",crystalHookState.load()==1 ? "installed" : crystalHookState.load()<0 ? "unavailable" : "pending");
    ImGui::BeginChild("##live-console-events", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);
    unsigned long long generation=0;auto lines=eclient_console::log.snapshot(generation);
    for(const auto& line:lines) ImGui::TextUnformatted(line.c_str());
    static unsigned long long lastGeneration=0;
    if(follow && generation!=lastGeneration) ImGui::SetScrollHereY(1.0f);
    lastGeneration=generation;
    ImGui::EndChild();
}
static void crystalSettings() {
    ImGui::TextWrapped("Gatyo-based auto placement and crystal breaking are independent toggles.");
    bool aura=breakAura.load();if(ImGui::Checkbox("Continuous crystal break", &aura)) {breakAura.store(aura);eclient_console::log.add("Crystal break %s",aura ? "enabled" : "disabled");}
    ImGui::TextWrapped("Attacks nearby crystals every half second. No player targeting or damage calculation.");
    if(ImGui::Button("Arm one crystal attack (10s)")) {
        deadlineMs.store(nowMs()+10000,std::memory_order_relaxed);
        crystalState.store(1,std::memory_order_release);
        eclient_console::log.add("One-shot crystal attack armed for 10 seconds");
    }
    ImGui::SameLine();if(ImGui::Button("Cancel attack")) cancelIfArmed();
    const char* status="Idle";
    switch(crystalState.load()) {case 1:status="Armed: waiting for crystal within 4 blocks";break;case 2:status="Expired: no crystal found";break;case 3:status="Attack attempted (not proof it broke)";break;case 4:status="Cancelled or world unavailable";break;}
    ImGui::TextWrapped("Crystal test: %s",status);
    ImGui::Separator();
    placementControls();
}
static void totemSettings() {
    ImGui::TextWrapped("Automatic equip uses Gatyo inventory transactions. The extra transfer call below is a separate manual test.");
    ImGui::TextWrapped("Inventory action test: arm, then manually move a totem into offhand. The next transfer call is invoked twice with the live arguments. It may fail, desync or change inventory; it is NOT automatic totem selection.");
    if(ImGui::Button("Arm extra transfer call (30s)")) {transferDeadline.store(nowMs()+30000);transferReplay.store(1);eclient_console::log.add("Extra transfer armed for 30s");}
    ImGui::SameLine();if(ImGui::Button("Cancel transfer")) {int expected=1;if(transferReplay.compare_exchange_strong(expected,4)) eclient_console::log.add("Extra transfer cancelled");}
    int t=transferReplay.load();
    ImGui::Text("Extra transfer: %s",t==1 ? "armed" : t==2 ? "expired" : t==3 ? "called" : t==4 ? "cancelled" : "idle");
    ImGui::Text("Transfer hook: %s",observerState.load()==1 ? "installed" : observerState.load()<0 ? "unavailable" : "pending");
    ImGui::Text("Captured transfer calls: %d",events.load());
    ImGui::TextWrapped("All captured transfer calls appear in Console; calls are not proof of a successful inventory move.");
}
}
