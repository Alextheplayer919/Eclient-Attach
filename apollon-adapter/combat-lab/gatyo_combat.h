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
#include "combat_modes.h"
namespace eclient_gatyo {
static std::atomic<bool> autoTotem{false}, autoPlace{false}, smartTotem{false}, surroundOnly{false}, cpvpMobs{false};
static std::atomic<int> crystalMode{0};
static std::atomic<float> totemHealth{10.f}, shieldHealth{16.f};
static std::atomic<int> delayMs{500}, totemDelayMs{1000};
static std::atomic<float> rangeBlocks{5.0f};
static long long lastTotem=0,lastPlacement=0,lastNoTotem=0;
static int pendingSlot=-1, pendingTicks=0, pendingItem=0;
static bool shieldLatch=false,previousSmart=false;
static eclient_combat_modes::Cycle cpvp;
static vec3_t cpvpTarget{};
static uintptr_t base() { static uintptr_t p=0; if(!p) p=getAbsoluteAddress(MineLib,0); return p; }
template<class T> static T fn(uintptr_t offset) {return reinterpret_cast<T>(base()+offset);}
static long long now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void leaveWorld() {lastTotem=lastPlacement=lastNoTotem=0;pendingSlot=-1;pendingTicks=0;shieldLatch=false;cpvp.reset();}
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
static bool surrounded(LocalPlayer* player) {
    auto* region=player->getDimensionBlockSource();auto* p=player->getPosition();if(!region || !p)return false;
    int x=int(std::floor(p->x)),y=int(std::floor(p->y-1.62f+.5f)),z=int(std::floor(p->z));
    const int sides[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(auto& side:sides){vec3_ti pos{x+side[0],y,z+side[1]};Block* b=region->getBlock(pos);if(!b || b->isAir())return false;}
    return true;
}
static void swapOffhand(LocalPlayer* player,void* container,int found,void* held,uintptr_t itemName,long long t) {
    void* stack=slot(container,found);
    if(!held || fn<bool(*)(void*)>(0xd3e3d80)(held)) held=reinterpret_cast<void*>(base()+0x10724d88);
    alignas(16) unsigned char from[480],to[480],transaction[64]{};
    buildAction(from,0,found,stack,held);buildAction(to,119,0,held,stack);
    float load=1.0f;std::memcpy(transaction+32,&load,sizeof(load));
    auto add=fn<void(*)(void*,void*)>(0xd4566f0);add(transaction,from);add(transaction,to);
    eclient_console::log.add("Auto Totem: sending %s from slot %d",itemName==0x10721d60?"shield":"totem",found);
    fn<void(*)(void*,void*)>(0x6bf3a80)(player,transaction);
    auto destroy=fn<void(*)(void*)>(0x58b50c8);destroy(from);destroy(to);
    fn<void(*)(void*)>(0x932ad80)(transaction);
    pendingSlot=found;pendingItem=itemName==0x10721d60?1:0;pendingTicks=10;lastTotem=t;
}
static void tryTotem(LocalPlayer* player) {
    if(!autoTotem.load()) {pendingSlot=-1;pendingTicks=0;shieldLatch=false;previousSmart=false;return;}
    if(!base()) return;
    bool smart=smartTotem.load();if(smart!=previousSmart){pendingSlot=-1;pendingTicks=0;shieldLatch=false;previousSmart=smart;}
    long long t=now();void* container=inventory(player);if(!container)return;
    void* held=fn<void*(*)(void*)>(0xc9486c8)(player);
    constexpr uintptr_t totem=0x10722418,shield=0x10721d60;
    if(smart && pendingTicks>0 && pendingSlot>=0) {
        uintptr_t pendingName=pendingItem?shield:totem;
        if(itemIs(held,pendingName)){pendingTicks=0;pendingSlot=-1;eclient_console::log.add("Auto Totem: offhand confirmed");}
        else {if(!itemIs(slot(container,pendingSlot),pendingName))pendingTicks=std::min(pendingTicks,4);if(--pendingTicks>0)return;pendingSlot=-1;}
    }
    uintptr_t wanted=totem;
    if(smart) {
        shieldLatch=eclient_combat_modes::shieldLatch(shieldLatch,float(player->getHealth()),totemHealth.load(),shieldHealth.load());
        if(shieldLatch && (!surroundOnly.load() || surrounded(player)) && (itemIs(held,shield) || findItem(container,shield,36)>=0))wanted=shield;
    }
    if(itemIs(held,wanted)) {if(pendingSlot>=0 && !smart)eclient_console::log.add("Auto Totem: totem observed in offhand");pendingSlot=-1;return;}
    if(t-lastTotem<totemDelayMs.load())return;
    int found=findItem(container,wanted,36);
    if(found<0) {if(t-lastNoTotem>=5000){lastNoTotem=t;eclient_console::log.add("Auto Totem: no %s in inventory",wanted==shield?"shield":"totem");}return;}
    swapOffhand(player,container,found,held,wanted,t);
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
// CPVP borrows a target identity only as an opaque comparison key. The aura
// revalidates the live actor every tick; no actor pointer is retained or used.
static bool cpvpActive() {return autoPlace.load() && crystalMode.load()==1;}
static bool cpvpNeedsHit() {return cpvpActive() && cpvp.target && cpvp.wantsHit() && cpvp.ready(now(),delayMs.load());}
static void cpvpTrack(std::uintptr_t key,const vec3_t& pos) {
    if(!cpvpActive() || !key){cpvp.reset();return;}
    cpvp.track(key,now());cpvpTarget=pos;
}
static void cpvpHit(std::uintptr_t key,const vec3_t& pos) {
    if(!cpvpNeedsHit())return;
    cpvpTarget=pos;cpvp.attacked(key,now());
    eclient_console::log.add("CPVP: hit attempted, starting crystals");
}
static bool airAt(BlockSource* region,const vec3_ti& p) {Block* b=region->getBlock(p);return b && b->isAir();}
static bool cellClearOfPlayer(const vec3_ti& cell,const vec3_t& eyes) {
    float x=eyes.x-(cell.x+.5f),z=eyes.z-(cell.z+.5f),feet=eyes.y-1.62f;
    return std::abs(x)>=.8f || std::abs(z)>=.8f || feet>=cell.y+3.f || feet+1.8f<=cell.y;
}
static bool cpvpFindBlockCell(BlockSource* region,const vec3_t& player,const vec3_t& target,vec3_ti& cell,vec3_ti& clicked) {
    const int tx=int(std::floor(target.x)),ty=int(std::floor(target.y-1.62f)),tz=int(std::floor(target.z));
    float best=1e9f;bool found=false;float reach=rangeBlocks.load();
    for(int dx=-2;dx<=2;dx++)for(int dz=-2;dz<=2;dz++) {
        if(!dx && !dz)continue;
        vec3_ti p{tx+dx,ty,tz+dz},below{p.x,p.y-1,p.z},a{p.x,p.y+1,p.z},b{p.x,p.y+2,p.z};
        float px=p.x+.5f-player.x,py=p.y-player.y,pz=p.z+.5f-player.z;
        if(px*px+py*py+pz*pz>reach*reach || !cellClearOfPlayer(p,player) || !cellClearOfPlayer(p,target))continue;
        if(!airAt(region,p) || !airAt(region,a) || !airAt(region,b))continue;
        Block* support=region->getBlock(below);if(!support || support->isAir())continue;
        float x=p.x+.5f-target.x,z=p.z+.5f-target.z,score=x*x+z*z;
        if(score<best){best=score;cell=p;clicked=below;found=true;}
    }
    return found;
}
static bool cpvpSwitchAndBuild(LocalPlayer* player,GameMode* game,void* ci,uintptr_t item,const vec3_ti& clicked,unsigned char face) {
    void* container=inventory(player);if(!container)return false;
    auto* pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(player)+1392);if(!pi)return false;
    int previous=*reinterpret_cast<int*>(pi+16);if(previous<0 || previous>=9)return false;
    int selected=itemIs(slot(container,previous),item)?previous:findItem(container,item,9);if(selected<0)return false;
    if(selected!=previous){*reinterpret_cast<int*>(pi+16)=selected;sendEquipment(player,ci,container,selected);}
    fn<void(*)(void*,const void*,unsigned char,bool)>(0xccc22a4)(game,&clicked,face,false);
    if(selected!=previous){*reinterpret_cast<int*>(pi+16)=previous;sendEquipment(player,ci,container,previous);}
    return true;
}
static Mob* cpvpCrystal(LocalPlayer* player,const vec3_t& pos,const std::vector<Mob*>& actors) {
    float range=rangeBlocks.load();
    for(Mob* actor:actors) {
        if(!actor || actor==player || actor->getEntityTypeId()!=71)continue;
        const vec3_t* p=actor->getPosition();if(!p)continue;
        float x=p->x-(cpvp.x+.5f),y=p->y-(cpvp.y+1.f),z=p->z-(cpvp.z+.5f);
        float px=p->x-pos.x,py=p->y-pos.y,pz=p->z-pos.z;
        if(x*x+y*y+z*z<=2.25f && px*px+py*py+pz*pz<=range*range)return actor;
    }return nullptr;
}
static void cpvpTick(LocalPlayer* player,GameMode* game,BlockSource* region,vec3_t* pos,void* ci,const std::vector<Mob*>& actors) {
    using eclient_combat_modes::Phase;
    if(!cpvpActive() || !base() || !player || !game || !region || !pos){cpvp.reset();return;}
    long long t=now();if(!cpvp.target)return;
    const int delay=delayMs.load();
    if(cpvp.expired(t,std::max(3500,delay+1500))){eclient_console::log.add("CPVP: phase timed out; retrying target");cpvp.reset();return;}
    if(cpvp.phase==Phase::hit)return;
    vec3_ti chosen{cpvp.x,cpvp.y,cpvp.z};
    if(cpvp.phase==Phase::base){
        vec3_ti cell{},clicked{};
        if(cpvpFindBlockCell(region,*pos,cpvpTarget,cell,clicked) && cpvpSwitchAndBuild(player,game,ci,0x10745510,clicked,1)) {
            cpvp.placeBase(cell.x,cell.y,cell.z,t);eclient_console::log.add("CPVP: obsidian placement attempted at %d %d %d",cell.x,cell.y,cell.z);return;
        }
        if(chooseBase(region,*pos,cpvpTarget,cell)){cpvp.x=cell.x;cpvp.y=cell.y;cpvp.z=cell.z;cpvp.baseReady(t);eclient_console::log.add("CPVP: using existing base");return;}
        if(cpvp.expired(t,1000)){eclient_console::log.add("CPVP: no suitable base or hotbar obsidian");cpvp.reset();}return;
    }
    if(cpvp.phase==Phase::waitBase){
        if(blockIsBase(region->getBlock(chosen))){cpvp.baseReady(t);eclient_console::log.add("CPVP: base observed");}
        else if(cpvp.expired(t,1200)){eclient_console::log.add("CPVP: obsidian not confirmed");cpvp.reset();}
        return;
    }
    if(cpvp.phase==Phase::crystal){
        if(!canPlace(region,chosen)){eclient_console::log.add("CPVP: base obstructed or missing");cpvp.reset();return;}
        if(!cpvp.ready(t,delay))return;
        if(!cpvpSwitchAndBuild(player,game,ci,0x1071f6a0,chosen,0)){eclient_console::log.add("CPVP: no crystal in hotbar");cpvp.reset();return;}
        cpvp.crystalSent(t);eclient_console::log.add("CPVP: crystal placement attempted");return;
    }
    Mob* crystal=cpvpCrystal(player,*pos,actors);
    if(cpvp.phase==Phase::waitCrystal){
        if(crystal){cpvp.crystalFound(t);return;}
        if(cpvp.expired(t,1200)){if(++cpvp.tries>=3){eclient_console::log.add("CPVP: crystal not observed");cpvp.reset();}else {cpvp.phase=Phase::crystal;cpvp.since=t;}}
        return;
    }
    if(cpvp.phase==Phase::waitBreak){
        if(!crystal && cpvp.tries==0){eclient_console::log.add("CPVP: crystal disappeared before attack; restarting");cpvp.reset();return;}
        if(!crystal){int completed=cpvp.burst+1;cpvp.broken(t);eclient_console::log.add("CPVP: crystal gone, burst %d/3",completed);return;}
        if(!cpvp.ready(t,delay))return;
        if(cpvp.tries>=3){eclient_console::log.add("CPVP: crystal did not disappear");cpvp.reset();return;}
        game->attack(crystal,true);cpvp.lastAction=t;++cpvp.tries;eclient_console::log.add("CPVP: crystal attack attempted");
    }
}
static void tick(LocalPlayer* player,Level* level,GameMode* game,BlockSource* region,vec3_t* pos,void* ci,const vec3_t* fakeTarget=nullptr) {
    if(!player || panic || inMenu)return;
    tryTotem(player);
    if(crystalMode.load()==0) {cpvp.reset();tryPlace(player,level,game,region,pos,ci,fakeTarget);}
}
static void crystalUI() {
    bool enabled=autoPlace.load();
    if(ImGui::Checkbox("Crystal Aura",&enabled)) {autoPlace.store(enabled);eclient_combat_lab::breakAura.store(enabled);eclient_console::log.add("Crystal Aura %s",enabled?"enabled":"disabled");}
    int mode=crystalMode.load();const char* modes[]={"Standard","CPVP"};if(ImGui::Combo("Mode##crystal",&mode,modes,2))crystalMode.store(mode);
    if(mode==1){
        bool targetMobs=cpvpMobs.load();if(ImGui::Checkbox("Target mobs in CPVP",&targetMobs))cpvpMobs.store(targetMobs);
        ImGui::TextWrapped("Enable Gatyo KillAura. Mob targeting uses its existing mob filters; players remain targets. Base first, then hit, then crystals. Explosions can damage you.");
    }
    int delay=delayMs.load();if(ImGui::SliderInt("Delay (ms)",&delay,100,3000)){delayMs.store(delay);eclient_combat_lab::breakDelay.store(delay);}
    float range=rangeBlocks.load();if(ImGui::SliderFloat("Range (blocks)",&range,2.0f,10.0f,"%.1f")){rangeBlocks.store(range);eclient_combat_lab::breakRange.store(range);}
}
static void totemUI() {
    bool enabled=autoTotem.load();if(ImGui::Checkbox("Auto Totem",&enabled)){autoTotem.store(enabled);eclient_console::log.add("Auto Totem %s",enabled?"enabled":"disabled");}
    int delay=totemDelayMs.load();if(ImGui::SliderInt("Delay (ms)##totem",&delay,250,3000))totemDelayMs.store(delay);
    bool smart=smartTotem.load();if(ImGui::Checkbox("Smart shield / totem",&smart))smartTotem.store(smart);
    if(smart){
        float low=totemHealth.load();if(ImGui::SliderFloat("Totem at or below (HP)",&low,0.f,20.f,"%.0f")){totemHealth.store(low);if(low>shieldHealth.load())shieldHealth.store(low);}
        float high=shieldHealth.load();if(ImGui::SliderFloat("Shield at or above (HP)",&high,0.f,20.f,"%.0f")){shieldHealth.store(high);if(high<totemHealth.load())totemHealth.store(high);}
        bool safe=surroundOnly.load();if(ImGui::Checkbox("Shield only when surrounded",&safe))surroundOnly.store(safe);
        ImGui::TextWrapped("Keeps a totem at low health; switches to a shield above the upper threshold if available.");
    }
}
}
