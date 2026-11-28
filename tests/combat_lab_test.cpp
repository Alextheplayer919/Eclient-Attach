#include <cstdint>
#include <cassert>
#include <vector>
#include <thread>
#include "../apollon-adapter/compat/placement_forward.h"
struct vec3_t {float x,y,z;};
class Mob {public:int type=0;vec3_t p{};int getEntityTypeId(){return type;}vec3_t* getPosition(){return &p;}};
class LocalPlayer:public Mob{};
class GameMode {public:int calls=0;Mob* last=nullptr;void attack(Mob* a,bool){++calls;last=a;}};
namespace ImGui {bool Button(const char*){return false;}void TextWrapped(const char*,...){}}
#include "../apollon-adapter/combat-lab/combat_lab.h"
int calls=0;
std::uint64_t forward(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uint32_t,std::uintptr_t,std::uintptr_t,std::uint32_t){++calls;return 42;}
int main(){
    using namespace eclient_combat_lab;
    LocalPlayer p;GameMode game;vec3_t pos{0,0,0};Mob near,far;near.type=far.type=71;near.p={2,0,0};far.p={5,0,0};std::vector<Mob*> actors{&near,&far};
    tick(&p,&game,&pos,actors);assert(game.calls==0);
    breakAura.store(true);breakRange.store(3);breakDelay.store(500);tick(&p,&game,&pos,actors);assert(game.calls==1 && game.last==&near);
    tick(&p,&game,&pos,actors);assert(game.calls==1);lastAttack=0;breakRange.store(1);tick(&p,&game,&pos,actors);assert(game.calls==1);
    eclient::compat::PlacementArgs args{};assert(placementExperiment(forward,args)==42 && calls==1);cancelIfArmed();install();drawConsole();
    unsigned long long gen=0;eclient_console::Log log;for(int i=0;i<105;i++)log.add("event %d",i);auto lines=log.snapshot(gen);assert(lines.size()==100 && lines.front().find("event 5")!=std::string::npos);
    log.clear();std::thread a([&]{for(int i=0;i<1000;i++)log.add("a");});std::thread b([&]{for(int i=0;i<1000;i++)log.add("b");});a.join();b.join();assert(log.snapshot(gen).size()==100);
}
