#pragma once
// Adapted from the user-supplied Gatyo source (crystal_common_gatyo.h,
// autocrystal_gatyo.h and offhand_gatyo.h), for pinned ARM64 1.21.111 only.
// These are game-thread operations. UI communicates through atomics.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
namespace eclient_gatyo {
static std::atomic<bool> autoTotem{false}, autoPlace{false};
static std::atomic<int> delayMs{500}, totemDelayMs{1000};
static std::atomic<float> rangeBlocks{5.0f};
static long long lastTotem=0,lastPlacement=0,lastNoTotem=0;
static int pendingSlot=-1;
static uintptr_t base() { static uintptr_t p=0; if(!p) p=getAbsoluteAddress(MineLib,0); return p; }
template<class T> static T fn(uintptr_t offset) {return reinterpret_cast<T>(base()+offset);}
static long long now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void leaveWorld() {lastTotem=lastPlacement=lastNoTotem=0;pendingSlot=-1;}
static void* inventory(LocalPlayer* p) {
    auto pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(p)+1392);
    return pi ? *reinterpret_cast<void**>(pi+184) : nullptr;
}
static void* slot(void* container,int index) {
    using Get=void*(*)(void*,int);return reinterpret_cast<Get>((*reinterpret_cast<void***>(container))[8])(container,index);
}
static bool itemIs(void* stack,uintptr_t name) {
    return stack && !fn<bool(*)(void*)>(0xd3e3d80)(stack) && fn<bool(*)(void*,void*,bool)>(0xd3e11ac)(stack,reinterpret_cast<void*>(base()+name),false);
}
static int findItem(void* container,uintptr_t name,int count) {
    for(int i=0;i<count;++i) if(itemIs(slot(container,i),name)) return i;
    return -1;
}
static void write32(unsigned char* p,std::uint32_t v) {std::memcpy(p,&v,sizeof(v));}
static void buildAction(unsigned char* action,std::uint8_t container,std::uint32_t index,void* from,void* to) {
    std::memset(action,0,480);action[4]=container;write32(action+12,index);
    auto desc=fn<void*(*)(void*,void*)>(0xd3f4ddc);
    auto copy=fn<void*(*)(void*,void*)>(0xd3dfe40);
    desc(action+16,from);desc(action+96,to);copy(action+176,from);copy(action+328,to);
}
static void tryTotem(LocalPlayer* player) {
    if(!autoTotem.load()) {pendingSlot=-1;return;}
    if(!base()) return;
    long long t=now();
    if(t-lastTotem<totemDelayMs.load()) return;
    void* container=inventory(player);if(!container) return;
    void* held=fn<void*(*)(void*)>(0xc9486c8)(player);
    if(itemIs(held,0x10722418)) {
        if(pendingSlot>=0) eclient_console::log.add("Auto Totem: totem observed in offhand");
        pendingSlot=-1;return;
    }
    int found=findItem(container,0x10722418,36);
    if(found<0) {if(t-lastNoTotem>=5000){lastNoTotem=t;eclient_console::log.add("Auto Totem: no totem in inventory");}return;}
    void* stack=slot(container,found);
    if(!held || fn<bool(*)(void*)>(0xd3e3d80)(held)) held=reinterpret_cast<void*>(base()+0x10724d88);
    alignas(16) unsigned char from[480],to[480],transaction[64]{};
    buildAction(from,0,found,stack,held);buildAction(to,119,0,held,stack);
    float load=1.0f;std::memcpy(transaction+32,&load,sizeof(load));
    auto add=fn<void(*)(void*,void*)>(0xd4566f0);
    add(transaction,from);add(transaction,to);
    eclient_console::log.add("Auto Totem: sending inventory slot %d to offhand",found);
    fn<void(*)(void*,void*)>(0x6bf3a80)(player,transaction);
    auto destroy=fn<void(*)(void*)>(0x58b50c8);destroy(from);destroy(to);
    fn<void(*)(void*)>(0x932ad80)(transaction);
    pendingSlot=found;lastTotem=t;
}
static bool blockIsBase(Block* b) {
    if(!b)return false;
    BlockType* type=b->getBlockTypeClassCustom();if(!type)return false;
    auto& name=*reinterpret_cast<const std::string*>(reinterpret_cast<char*>(type)+8);
    auto split=name.find_last_of(".:");auto id=name.substr(split==std::string::npos?0:split+1);
    return id=="obsidian" || id=="bedrock";
}
static bool canPlace(BlockSource* region,const vec3_ti& p) {
    if(!blockIsBase(region->getBlock(p))) return false;
    vec3_ti above{p.x,p.y+1,p.z},above2{p.x,p.y+2,p.z};
    Block* a=region->getBlock(above);Block* b=region->getBlock(above2);
    return a && b && a->isAir() && b->isAir();
}
static void sendEquipment(LocalPlayer* player,void* ci,void* container,int selected) {
    alignas(16) unsigned char packet[0xa8]{};
    auto rid=fn<std::uint64_t(*)(Actor*)>(0xc938058)(player);
    void* item=slot(container,selected);
    if(!item)item=reinterpret_cast<void*>(base()+0x10724d88);
    fn<void*(*)(void*,std::uint64_t,void*,int,int,int)>(0xa847dd0)(packet,rid,item,selected,selected,0);
    if(ci) {
        void** vt=*reinterpret_cast<void***>(ci);
        void* sender=reinterpret_cast<void*(*)(void*)>(vt[2400/8])(ci);
        if(sender) reinterpret_cast<void(*)(void*,void*)>((*reinterpret_cast<void***>(sender))[24/8])(sender,packet);
        else reinterpret_cast<void(*)(void*,void*)>((*reinterpret_cast<void***>(player))[0x738/8])(player,packet);
    } else reinterpret_cast<void(*)(void*,void*)>((*reinterpret_cast<void***>(player))[0x738/8])(player,packet);
    reinterpret_cast<void(*)(void*)>((*reinterpret_cast<void***>(packet))[0])(packet);
}
static bool chooseBase(BlockSource* region,const vec3_t& me,const vec3_t& target,vec3_ti& best) {
    const float range=rangeBlocks.load();
    int tx=static_cast<int>(std::floor(target.x)),ty=static_cast<int>(std::floor(target.y)),tz=static_cast<int>(std::floor(target.z));
    float score=1e9f;bool found=false;
    for(int dx=-3;dx<=3;++dx)for(int dz=-3;dz<=3;++dz)for(int dy=-3;dy<=2;++dy) {
        vec3_ti p{tx+dx,ty+dy,tz+dz};
        float px=p.x+0.5f-me.x,py=p.y+1.0f-me.y,pz=p.z+0.5f-me.z;
        float d=px*px+py*py+pz*pz;if(d>range*range || d<4.0f)continue;
        float x=p.x+0.5f-target.x,y=p.y+1.0f-target.y,z=p.z+0.5f-target.z;
        float targetDist=x*x+y*y+z*z;if(targetDist>36.0f || targetDist>=score)continue;
        if(canPlace(region,p)){score=targetDist;best=p;found=true;}
    }
    return found;
}
static void tryPlace(LocalPlayer* player,Level* level,GameMode* game,BlockSource* region,vec3_t* pos,void* ci,const vec3_t* fakeTarget=nullptr) {
    if(!autoPlace.load() || !base() || !level || !game || !region || !pos) return;
    long long t=now();if(t-lastPlacement<delayMs.load())return;
    auto* manager=level->getGameplayUserManager();if(!manager)return;
    vec3_t target{};bool hasTarget=false;float closest=100.0f;
    manager->forEachActivePlayer([&](Player& p) {
        if(&p==player || p.getHealth()<=0) return true;
        const auto& name=p.getName();
        if(std::find(friendList.begin(),friendList.end(),name)!=friendList.end() || std::find(botList.begin(),botList.end(),name)!=botList.end()) return true;
        auto* at=p.getPosition();if(!at)return true;
        float x=at->x-pos->x,y=at->y-pos->y,z=at->z-pos->z;float dist=x*x+y*y+z*z;
        if(std::isfinite(dist) && dist<closest){target=*at;closest=dist;hasTarget=true;}
        return true;
    },true);
    if(!hasTarget && fakeTarget) {target=*fakeTarget;hasTarget=true;}
    if(!hasTarget)return;
    void* container=inventory(player);if(!container)return;
    auto* pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(player)+1392);
    int previous=*reinterpret_cast<int*>(pi+16);
    int crystal=(previous>=0 && previous<9 && itemIs(slot(container,previous),0x1071f6a0))?previous:findItem(container,0x1071f6a0,9);
    if(crystal<0) {lastPlacement=t;eclient_console::log.add("Crystal Aura: no crystals in hotbar");return;}
    vec3_ti candidate{};if(!chooseBase(region,*pos,target,candidate)) {lastPlacement=t;return;}
    if(crystal!=previous) {*reinterpret_cast<int*>(pi+16)=crystal;sendEquipment(player,ci,container,crystal);}
    eclient_console::log.add("Crystal Aura: buildBlock at %d %d %d (slot %d)",candidate.x,candidate.y,candidate.z,crystal);
    lastPlacement=t;
    fn<void(*)(void*,const void*,unsigned char,bool)>(0xccc22a4)(game,&candidate,0,false);
    if(crystal!=previous && previous>=0 && previous<9) {*reinterpret_cast<int*>(pi+16)=previous;sendEquipment(player,ci,container,previous);}
}
static void tick(LocalPlayer* player,Level* level,GameMode* game,BlockSource* region,vec3_t* pos,void* ci,const vec3_t* fakeTarget=nullptr) {
    if(!player || panic || inMenu)return;
    tryTotem(player);
    tryPlace(player,level,game,region,pos,ci,fakeTarget);
}
static void crystalUI() {
    bool enabled=autoPlace.load();
    if(ImGui::Checkbox("Crystal Aura",&enabled)) {autoPlace.store(enabled);eclient_combat_lab::breakAura.store(enabled);eclient_console::log.add("Crystal Aura %s",enabled?"enabled":"disabled");}
    int delay=delayMs.load();if(ImGui::SliderInt("Delay (ms)",&delay,100,3000)){delayMs.store(delay);eclient_combat_lab::breakDelay.store(delay);}
    float range=rangeBlocks.load();if(ImGui::SliderFloat("Range (blocks)",&range,2.0f,10.0f,"%.1f")){rangeBlocks.store(range);eclient_combat_lab::breakRange.store(range);}
}
static void totemUI() {
    bool enabled=autoTotem.load();if(ImGui::Checkbox("Auto Totem",&enabled)){autoTotem.store(enabled);eclient_console::log.add("Auto Totem %s",enabled?"enabled":"disabled");}
    int delay=totemDelayMs.load();if(ImGui::SliderInt("Delay (ms)##totem",&delay,250,3000))totemDelayMs.store(delay);
}
}
