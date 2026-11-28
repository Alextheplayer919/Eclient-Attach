#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
struct vec3_t{float x=0,y=0,z=0;};struct vec2_t{float x=0,y=0;};
struct Aabb {vec3_t lower,upper;vec2_t dims;};
struct Actor {vec3_t state[3]{};vec2_t rot{};Aabb box{{-.3f,0,-.3f},{.3f,1.8f,.3f},{.6f,1.8f}};int type=319;
    vec3_t* getPosition(){return state;}vec2_t* getRotation(){return &rot;}vec2_t* getAABBDim(){return &box.dims;}int getEntityTypeId(){return type;}
    void setRotationX(float x){rot.x=x;}void setRotationY(float y){rot.y=y;}void setYHeadRot(float){}void setYBodyRotation(float){} };
struct Mob:Actor {int getHealth(){return 20;}int getHurtTime(){return 0;}};
struct Player:Mob {std::string name="enemy";const std::string& getName(){return name;}const std::string& getNameTag(){return name;}bool isAttackableGamemode(){return true;}};
struct LocalPlayer:Player {void swing(){}};
struct GameplayUserManager {Player* p=nullptr;void forEachActivePlayer(std::function<bool(Player&)> f,bool){if(p)f(*p);}};
struct Level {GameplayUserManager manager;GameplayUserManager* getGameplayUserManager(){return &manager;}};
struct GameMode {int hits=0;void attack(Actor*,bool){++hits;}};
namespace Fields64 {constexpr int GetPosition=0x208,ActorRotation=0x218;}
namespace eclient_bypass_fly {float g_moveInputX=0,g_moveInputZ=0,g_moveDeadzone=.0006f;}
bool panic=false,inMenu=false,antiBot=false;std::vector<std::string> friendList,botList;
bool MobDetected(int){return true;}
template<class T>T ImClamp(T v,T a,T b){return std::clamp(v,a,b);}
namespace ImGui {bool Checkbox(const char*,bool*){return false;}bool Combo(const char*,int*,const char* const[],int){return false;}bool SliderFloat(const char*,float*,float,float,const char*){return false;}bool SliderInt(const char*,int*,int,int){return false;}void TextWrapped(const char*,...){}}
#include "../apollon-adapter/gatyo-aura/gatyo_aura.h"
int main(){
    using namespace eclient_gatyo_aura;
    LocalPlayer player;Player target;target.state[0]={2,0,1};Level level;level.manager.p=&target;GameMode gm;std::vector<Mob*> actors;
    tick(&player,&level,&gm,actors);assert(gm.hits==0);enabled=true;range=4;
    for(int i=0;i<GAX_ROT_COUNT;i++){rotationMode=i;for(int j=0;j<5;j++)tick(&player,&level,&gm,actors);auto p=current();
        if(i==GAX_ROT_NONE || i==GAX_ROT_STRAFE)assert(!p.active);else {assert(p.active);assert(std::isfinite(p.pitch)&&std::isfinite(p.yaw));}}
    assert(gm.hits==GAX_ROT_COUNT*5);char packet[256]{};applyPacket(packet);float yaw;std::memcpy(&yaw,packet+0x30,4);assert(std::isfinite(yaw));applyLocal(&player);
    friendList.push_back("enemy");tick(&player,&level,&gm,actors);assert(!current().active);friendList.clear();ui();reset();assert(!current().active);
}
