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
int attackCount=0;struct GameMode {int hits=0;Actor* last=nullptr;void attack(Actor* a,bool){++hits;last=a;++attackCount;}};
namespace Fields64 {constexpr int GetPosition=0x208,ActorRotation=0x218;}
namespace eclient_bypass_fly {float g_moveInputX=0,g_moveInputZ=0,g_moveDeadzone=.0006f;}
bool panic=false,inMenu=false,antiBot=false;std::vector<std::string> friendList,botList;
bool MobDetected(int){return true;}
template<class T>T ImClamp(T v,T a,T b){return std::clamp(v,a,b);}
namespace ImGui {bool Checkbox(const char*,bool*){return false;}bool Combo(const char*,int*,const char* const[],int){return false;}bool SliderFloat(const char*,float*,float,float,const char*){return false;}bool SliderInt(const char*,int*,int,int){return false;}void TextWrapped(const char*,...){}}
#ifdef ECLIENT_COMBAT_LAB
namespace eclient_fake_sim {struct Vec {float x=0,y=0,z=0;};struct State {Vec feet{},velocity{};float pitch=0,yaw=0;};}
namespace eclient_fake {bool auraSnapshot(eclient_fake_sim::State&,bool&,Actor*&){return false;}void auraHit(LocalPlayer*){++attackCount;}}
namespace eclient_gatyo {
    std::atomic<bool> cpvpMobs{false};bool active=false,needsHit=true;std::uintptr_t tracked=0,hit=0;vec3_t trackedPos{};int attacksAtHit=0;
    bool cpvpActive(){return active;}bool cpvpNeedsHit(){return active && needsHit;}
    void cpvpTrack(std::uintptr_t key,const vec3_t& pos){tracked=key;trackedPos=pos;}
    void cpvpHit(std::uintptr_t key,const vec3_t&){hit=key;attacksAtHit=attackCount;needsHit=false;}
}
#endif
#include "../apollon-adapter/gatyo-aura/gatyo_aura.h"
int main(){
    using namespace eclient_gatyo_aura;
    LocalPlayer player;Player target;target.state[0]={2,0,1};Level level;level.manager.p=&target;GameMode gm;std::vector<Mob*> actors;
    tick(&player,&level,&gm,actors);assert(gm.hits==0);enabled=true;range=4;
    for(int i=0;i<GAX_ROT_COUNT;i++){rotationMode=i;for(int j=0;j<5;j++)tick(&player,&level,&gm,actors);auto p=current();
        if(i==GAX_ROT_NONE || i==GAX_ROT_STRAFE)assert(!p.active);else {assert(p.active);assert(std::isfinite(p.pitch)&&std::isfinite(p.yaw));}}
    assert(gm.hits==GAX_ROT_COUNT*5);char packet[256]{};applyPacket(packet);float yaw;std::memcpy(&yaw,packet+0x30,4);assert(std::isfinite(yaw));applyLocal(&player);
#ifdef ECLIENT_COMBAT_LAB
    reset();level.manager.p=nullptr;Mob mob;mob.type=4875;mob.state[0]={2,0,1};mob.box.lower.y=-1.25f;
    Mob crystal;crystal.type=71;crystal.state[0]={.5f,0,0};actors={&mob,&crystal};
    eclient_gatyo::active=true;eclient_gatyo::needsHit=true;eclient_gatyo::cpvpMobs=false;mobs=false;interval=3;rotationMode=GAX_ROT_NONE;
    int before=gm.hits;tick(&player,&level,&gm,actors);assert(gm.hits==before && eclient_gatyo::tracked==0);
    eclient_gatyo::cpvpMobs=true;tick(&player,&level,&gm,actors);tick(&player,&level,&gm,actors);assert(gm.hits==before);
    tick(&player,&level,&gm,actors);assert(gm.hits==before+1 && gm.last==&mob);
    assert(eclient_gatyo::hit==reinterpret_cast<std::uintptr_t>(&mob) && eclient_gatyo::attacksAtHit==attackCount);
    assert(std::abs(eclient_gatyo::trackedPos.y-(-1.25f+1.62f))<.0001f);
    for(int i=0;i<10;++i){tick(&player,&level,&gm,actors);}assert(gm.hits==before+1);
    eclient_gatyo::needsHit=true;tick(&player,&level,&gm,actors);tick(&player,&level,&gm,actors);assert(gm.hits==before+1);
    tick(&player,&level,&gm,actors);assert(gm.hits==before+2 && gm.last==&mob);
    eclient_gatyo::active=false;eclient_gatyo::cpvpMobs=false;actors.clear();interval=1;level.manager.p=&target;
#endif
    friendList.push_back("enemy");tick(&player,&level,&gm,actors);assert(!current().active);friendList.clear();ui();reset();assert(!current().active);
}
