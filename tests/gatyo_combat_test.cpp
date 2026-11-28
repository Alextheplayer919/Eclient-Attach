#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
struct vec3_t {float x,y,z;}; struct vec3_ti {int x,y,z;};
struct BlockType {std::uint64_t hash=0;std::string name;};
struct Block {BlockType* type=nullptr; bool air=true;BlockType* getBlockTypeClassCustom(){return type;}bool isAir(){return air;}};
struct BlockSource {BlockType obs{0,"tile.obsidian"},cry{0,"tile.crying_obsidian"};Block block{&obs,false},air{nullptr,true};Block* getBlock(const vec3_ti& p){return p.x==3 && p.y==0 && p.z==0 ? &block:&air;}};
struct Actor {};struct Player:Actor {vec3_t pos{0,1,0};std::string name="Player";int getHealth(){return 20;}vec3_t* getPosition(){return &pos;}const std::string& getName(){return name;}};
struct LocalPlayer:Player{};struct GameMode{};
struct GameplayUserManager {void forEachActivePlayer(std::function<bool(Player&)>,bool){}};
struct Level {GameplayUserManager manager;GameplayUserManager* getGameplayUserManager(){return &manager;}};
std::vector<std::string> friendList,botList;bool panic=false,inMenu=false;
const char* MineLib="unused";uintptr_t getAbsoluteAddress(const char*,uintptr_t){return 0;}
namespace eclient_combat_lab {std::atomic<bool> breakAura{false};std::atomic<int> breakDelay{500};std::atomic<float> breakRange{5};}
namespace ImGui {bool SliderFloat(const char*,float*,float,float,const char*){return false;}bool Checkbox(const char*,bool*){return false;}bool SliderInt(const char*,int*,int,int){return false;}void TextWrapped(const char*,...){}}
#include "../apollon-adapter/combat-lab/live_console.h"
#include "../apollon-adapter/combat-lab/gatyo_combat.h"
int main() {
    using namespace eclient_gatyo;
    BlockSource region;vec3_t me{0,1,0},target{3,1,0};vec3_ti result{};
    assert(chooseBase(&region,me,target,result));assert(result.x==3 && result.y==0);
    assert(blockIsBase(&region.block));region.block.type=&region.cry;assert(!blockIsBase(&region.block));
    assert(!chooseBase(&region,me,target,result));
    LocalPlayer player;Level level;GameMode mode;tick(&player,&level,&mode,&region,&me,nullptr);
    crystalUI();totemUI();leaveWorld();
}
