#include <cstdint>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <thread>
#include "../apollon-adapter/compat/placement_forward.h"
bool panic=false;
struct vec3_t {float x,y,z;};
class Mob {public: int type=0;vec3_t p{};int getEntityTypeId(){return type;} vec3_t* getPosition(){return &p;}};
class LocalPlayer : public Mob {};
class GameMode {public: int calls=0;Mob* last=nullptr;void attack(Mob* a,bool){++calls;last=a;}};
struct ImVec2 {ImVec2(float,float){}};
const int ImGuiWindowFlags_HorizontalScrollbar=1;
namespace ImGui {void BeginChild(const char*,ImVec2,bool,int){}void EndChild(){}void TextUnformatted(const char*){}void SetScrollHereY(float){}bool Button(const char*){return false;}void TextWrapped(const char*,...){} void Text(const char*,...){}void SameLine(){}void Separator(){}bool Checkbox(const char*,bool*){return false;}bool Combo(const char*,int*,const char*){return false;}}
#define ANDROID_LOG_INFO 1
#define ANDROID_LOG_ERROR 2
int __android_log_print(int,const char*,const char*,...){return 0;}
const char* MineLib="unused";
std::uintptr_t getAbsoluteAddress(const char*,std::uintptr_t){return 0;}
void A64HookFunction(void*,void*,void**){}
#include "../apollon-adapter/combat-lab/combat_lab.h"
int placementCalls=0;std::int32_t lastX=0;std::vector<std::int32_t> positions;std::vector<float> hits;
std::uint64_t placement(std::uintptr_t,std::uintptr_t,std::uintptr_t pos,std::uint32_t,std::uintptr_t hit,std::uintptr_t,std::uint32_t){++placementCalls;lastX=*reinterpret_cast<std::int32_t*>(pos);positions.push_back(lastX);if(hit) hits.push_back(*reinterpret_cast<float*>(hit));return 42;}
std::vector<std::uint64_t> crystalPositions;std::vector<std::uint32_t> crystalZ;
std::uint64_t crystalFn(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint64_t xy,std::uint32_t z,std::uintptr_t,std::uintptr_t,std::uintptr_t){crystalPositions.push_back(xy);crystalZ.push_back(z);return 7;}
int forwarded=0;
void proxy(void*,void*,std::uintptr_t){++forwarded;}
int main(){
    using namespace eclient_combat_lab;
    LocalPlayer player;GameMode game;vec3_t pos{0,0,0};Mob near,far,other;
    near.type=71;near.p={2,0,0};far.type=71;far.p={5,0,0};other.type=5;other.p={1,0,0};
    std::vector<Mob*> actors{&other,&far,&near};
    deadlineMs.store(nowMs()+10000);crystalState.store(1);tick(&player,&game,&pos,actors);
    assert(game.calls==1 && game.last==&near && crystalState.load()==3);
    tick(&player,&game,&pos,actors);assert(game.calls==1);
    deadlineMs.store(nowMs()-1);crystalState.store(1);tick(&player,&game,&pos,actors);assert(crystalState.load()==2);
    crystalState.store(1);cancelIfArmed();assert(crystalState.load()==4);
    originalTransfer=proxy;observing.store(true);transferObserver(&game,&near,0);
    assert(forwarded==1 && events.load()==1 && withContext.load()==1 && withInput.load()==1);
    observing.store(false);transferObserver(nullptr,nullptr,0);assert(forwarded==2 && events.load()==1);
    transferDeadline.store(nowMs()+1000);transferReplay.store(1);
    transferObserver(&game,&near,1);assert(forwarded==4 && transferReplay.load()==3);
    install();assert(observerState.load()==-1);
    std::int32_t posBlock[3]={1,2,3};eclient::compat::PlacementArgs a{0,0,reinterpret_cast<std::uintptr_t>(posBlock),0,0,0,0};
    placeDirection.store(0);placeDeadline.store(nowMs()+1000);placeState.store(1);
    assert(placementExperiment(placement,a)==42 && placementCalls==2 && positions[0]==2 && positions[1]==1 && posBlock[0]==1 && placeState.load()==3);
    placementExperiment(placement,a);assert(placementCalls==3);
    placeOrder.store(1);placeDeadline.store(nowMs()+1000);placeState.store(1);
    placementExperiment(placement,a);assert(placementCalls==5 && positions[3]==1 && positions[4]==2);
    float hit[3]={1.25f,2.5f,3.5f};a.a4=reinterpret_cast<std::uintptr_t>(hit);
    placeOrder.store(0);placeDeadline.store(nowMs()+1000);placeState.store(1);
    placementExperiment(placement,a);assert(hits.size()==2 && hits[0]==2.25f && hits[1]==1.25f && hit[0]==1.25f);
    originalCrystalItem=crystalFn;placeMethod.store(1);placeDirection.store(2);placeDeadline.store(nowMs()+1000);placeState.store(1);
    auto packed=std::uint64_t(1) | (std::uint64_t(2)<<32);
    assert(crystalItemHook(0,0,0,packed,3,0,0,0)==7);
    assert(crystalPositions.size()==2 && crystalPositions[0]==packed && crystalZ[0]==4 && crystalZ[1]==3);
    placeOrder.store(1);placeDirection.store(1);placeState.store(1);placeDeadline.store(nowMs()+1000);
    crystalItemHook(0,0,0,packed,3,0,0,0);
    assert(crystalPositions[2]==packed && crystalPositions[3]==(std::uint64_t(2)<<32) && crystalZ[3]==3);
    placeMethod.store(0);
    settings();drawConsole();
    unsigned long long generation=0;
    auto lines=eclient_console::log.snapshot(generation);assert(!lines.empty());
    eclient_console::Log log;
    for(int i=0;i<105;i++) log.add("event %d",i);
    auto bounded=log.snapshot(generation);assert(bounded.size()==100 && bounded.front().find("event 5")!=std::string::npos);
    log.clear();assert(log.snapshot(generation).empty());
    std::thread athread([&]{for(int i=0;i<1000;i++)log.add("worker %d",i);});
    std::thread bthread([&]{for(int i=0;i<1000;i++)log.add("other %d",i);});
    athread.join();bthread.join();assert(log.snapshot(generation).size()==100);
    placeState.store(1);placeDeadline.store(nowMs()-1);placementControls();assert(placeState.load()==2);
}
