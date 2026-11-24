#include <cstdint>
#include <cassert>
#include <cstdarg>
#include <vector>
#include "../apollon-adapter/compat/placement_forward.h"
bool panic=false;
struct vec3_t {float x,y,z;};
class Mob {public: int type=0;vec3_t p{};int getEntityTypeId(){return type;} vec3_t* getPosition(){return &p;}};
class LocalPlayer : public Mob {};
class GameMode {public: int calls=0;Mob* last=nullptr;void attack(Mob* a,bool){++calls;last=a;}};
namespace ImGui {bool Button(const char*){return false;}void TextWrapped(const char*,...){} void Text(const char*,...){}void SameLine(){}void Separator(){}bool Checkbox(const char*,bool*){return false;}bool Combo(const char*,int*,const char*){return false;}}
#define ANDROID_LOG_INFO 1
#define ANDROID_LOG_ERROR 2
int __android_log_print(int,const char*,const char*,...){return 0;}
const char* MineLib="unused";
std::uintptr_t getAbsoluteAddress(const char*,std::uintptr_t){return 0;}
void A64HookFunction(void*,void*,void**){}
#include "../apollon-adapter/combat-lab/combat_lab.h"
int placementCalls=0;std::int32_t lastX=0;
std::uint64_t placement(std::uintptr_t,std::uintptr_t,std::uintptr_t pos,std::uint32_t,std::uintptr_t,std::uintptr_t,std::uint32_t){++placementCalls;lastX=*reinterpret_cast<std::int32_t*>(pos);return 42;}
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
    install();assert(observerState.load()==-1);
    std::int32_t posBlock[3]={1,2,3};eclient::compat::PlacementArgs a{0,0,reinterpret_cast<std::uintptr_t>(posBlock),0,0,0,0};
    placeDirection.store(0);placeDeadline.store(nowMs()+1000);placeState.store(1);
    assert(placementExperiment(placement,a)==42 && placementCalls==2 && lastX==2 && posBlock[0]==1 && placeState.load()==3);
    placementExperiment(placement,a);assert(placementCalls==3);
    settings();
}
