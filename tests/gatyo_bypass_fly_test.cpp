#include <atomic>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
struct vec3_t {float x=0,y=0,z=0;};struct vec2_t {float x=0,y=0;};struct vec3_ti {int x,y,z;};
struct BlockType{};struct Block{bool isAir(){return true;}BlockType* getBlockTypeClassCustom(){return nullptr;}};
struct BlockSource{Block block;Block* getBlock(vec3_ti){return &block;}};
struct Actor {vec3_t state[3]{};vec2_t rot{},dim{.6f,1.8f};vec3_t* getPosition(){return &state[0];}vec2_t* getRotation(){return &rot;}vec2_t* getAABBDim(){return &dim;}};
struct LocalPlayer:Actor {BlockSource region;BlockSource* getDimensionBlockSource(){return &region;}};
LocalPlayer* localPlayer=nullptr;bool panic=false,inMenu=false;
namespace eclient_fly2_adapter {std::atomic<bool> enabled{false};}
template<class T> T ImMax(T a,T b){return std::max(a,b);}template<class T> T ImMin(T a,T b){return std::min(a,b);}template<class T> T ImClamp(T a,T lo,T hi){return std::clamp(a,lo,hi);}
namespace ImGui {bool SliderFloat(const char*,float*,float,float,const char*){return false;}bool Checkbox(const char*,bool*){return false;}void TextWrapped(const char*,...){}}
#include "../apollon-adapter/fly/gatyo_bypass_fly.h"
int main(){
    using namespace eclient_bypass_fly;
    LocalPlayer player;localPlayer=&player;bypassFly.store(true);
    FlyInput moving{true,1,0,true,false};TickBypassFly(&player,moving);
    assert(std::abs(player.state[2].x-bfInitBypH)<.001f);
    assert(std::abs(player.state[2].y-(bfGlide+bfInitBypV))<.001f);
    FlyInput descending{false,0,0,false,true};TickBypassFly(&player,descending);
    assert(player.state[2].x==0 && std::abs(player.state[2].y+bfOnlyShift)<.001f);
    uint64_t bits[2]={0,0};AuthMark(bits,AUTH_SNEAKING);FlyDecideAuthBits(bits);assert(g_authClear[0]&(1ull<<AUTH_SNEAKING));
    alignas(8) unsigned char packet[200]{};uint64_t saved[2]{};MoveInputOnAuthPacket(packet);FlyFilterAuthBits(packet,saved);FlyRestoreAuthBits(packet,saved);
    MoveInputRecord(&player);MoveInputOnGravity(&player.state[0]);MoveInputUpdate(&player);LookTrackBegin(&player);LookTrackEnd(&player,false);FlyOnNormalTick(&player);ui();reset();
}
