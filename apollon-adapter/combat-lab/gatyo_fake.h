#pragma once
// Gatyo-inspired client NPC and virtual fallback; 1.21.111 arm64 only.
#include "fake_simulation.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
namespace eclient_fake {
using namespace eclient_fake_sim;
using Handle=void(*)(void*,void*,void*,void*);using Packet=std::shared_ptr<char>;
static std::atomic<void*> callback{nullptr},source{nullptr},npcActor{nullptr};
static std::atomic<int> request{0}, armor{2},protection{4},totemCount{2};
static std::atomic<bool> moving{false},infinite{false},noKnock{false},gapples{true};
static std::atomic<int> status{0},shownHp{20},shownTotems{2}; // off, waiting, NPC, virtual, dead
static uintptr_t vtables[10]{};static Handle originals[10]{};static int hooks=0;
static std::uint64_t serial=0,runtime=0;static int waitTicks=0,equipKey=-1;static bool enabled=false,npcSent=false,virtualMode=false;
static State sim;static std::mutex mutex;
static bool (*oldAttack)(GameMode*,Actor*,bool)=nullptr;
static std::uint64_t (*oldInput)(ClientInstance*)=nullptr;
static Settings settings(){return {armor.load(),protection.load(),totemCount.load(),infinite.load(),moving.load(),noKnock.load(),gapples.load()};}
static void capture(void* self,void* src,void* cb,void* packet) {
    source.store(src,std::memory_order_release);callback.store(cb,std::memory_order_release);
    uintptr_t vt=*reinterpret_cast<uintptr_t*>(self);
    for(int i=0;i<hooks;i++)if(vtables[i]==vt){originals[i](self,src,cb,packet);return;}
}
static Packet create(uintptr_t offset) {return eclient_gatyo::fn<Packet(*)()>(offset)();}
static void deliver(Packet& packet) {
    auto cb=callback.load(std::memory_order_acquire);auto src=source.load(std::memory_order_acquire);
    if(!packet || !cb)return;
    void* dispatcher=*reinterpret_cast<void**>(packet.get()+0x20);
    if(dispatcher) reinterpret_cast<Handle>((*reinterpret_cast<void***>(dispatcher))[2])(dispatcher,src,cb,&packet);
}
static void event(std::uint8_t id) {
    if(!npcSent || !callback.load())return;
    auto p=create(0x9c1b9b8);if(!p)return;
    std::memcpy(p.get()+48,&runtime,8);p.get()[56]=static_cast<char>(id);
    int zero=0;std::memcpy(p.get()+60,&zero,4);deliver(p);
}
static void notify(int result) {
    if(result==1)event(2);
    if(result==2){event(65);equipKey=-1;eclient_console::log.add("Fake Player: totem pop, %d remaining",sim.totems);}
    if(result==3){event(3);eclient_console::log.add("Fake Player: defeated, respawning in 4 seconds");}
    shownHp.store(int(std::ceil(sim.health)));shownTotems.store(sim.totems);
}
static float heldDamage(LocalPlayer* player) {
    if(!player)return 1;auto* pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(player)+1392);
    auto container=eclient_gatyo::inventory(player);if(!pi||!container)return 1;
    int selected=*reinterpret_cast<int*>(pi+16);if(selected<0||selected>8)return 1;
    void* stack=eclient_gatyo::slot(container,selected);
    if(!stack||eclient_gatyo::fn<bool(*)(void*)>(0xd3e3d80)(stack))return 1;
    void** weak=*reinterpret_cast<void***>(reinterpret_cast<char*>(stack)+8);void* item=weak?*weak:nullptr;
    if(!item)return 1;int damage=reinterpret_cast<int(*)(void*)>((*reinterpret_cast<void***>(item))[37])(item);
    auto* position=player->getPosition();float fall=position?reinterpret_cast<const vec3_t*>(reinterpret_cast<const char*>(position)+24)->y:0;
    return std::max(1,damage)*(fall<-.1f?1.5f:1.f);
}
static bool attack(GameMode* gm,Actor* target,bool arg) {
    if(target && target==npcActor.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> guard(mutex);
        if(enabled && target==npcActor.load()) {
            LocalPlayer* lp=localPlayer;vec3_t* p=lp?lp->getPosition():nullptr;
            if(p)notify(sim.damage(heldDamage(lp),{p->x,p->y,p->z},settings()));
            return true; // Never send the NPC attack to the server.
        }
    }
    return oldAttack?oldAttack(gm,target,arg):false;
}
static bool solid(BlockSource* region,float x,float y,float z) {
    if(!region)return false;vec3_ti p{int(std::floor(x)),int(std::floor(y)),int(std::floor(z))};
    Block* block=region->getBlock(p);return block && !block->isAir();
}
static bool hitVirtual(LocalPlayer* player) {
    std::lock_guard<std::mutex> guard(mutex);
    if(!enabled || !virtualMode || sim.dead || !player)return false;
    auto* pos=player->getPosition();auto* rotation=player->getRotation();if(!pos||!rotation)return false;
    float pitch=rotation->x*.0174532925f,yaw=rotation->y*.0174532925f;
    float origin[]={pos->x,pos->y,pos->z},dir[]={-std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    float lo[]={sim.feet.x-.4f,sim.feet.y-.1f,sim.feet.z-.4f},hi[]={sim.feet.x+.4f,sim.feet.y+1.9f,sim.feet.z+.4f};
    float tmin=0,tmax=3.5f;
    for(int i=0;i<3;i++){if(std::abs(dir[i])<1e-6f){if(origin[i]<lo[i]||origin[i]>hi[i])return false;continue;}float a=(lo[i]-origin[i])/dir[i],b=(hi[i]-origin[i])/dir[i];if(a>b)std::swap(a,b);tmin=std::max(tmin,a);tmax=std::min(tmax,b);if(tmin>tmax)return false;}
    BlockSource* region=player->getDimensionBlockSource();
    for(float t=.1f;t<tmin;t+=.1f)if(solid(region,origin[0]+dir[0]*t,origin[1]+dir[1]*t,origin[2]+dir[2]*t))return false;
    notify(sim.damage(heldDamage(player),{pos->x,pos->y,pos->z},settings()));player->swing();return true;
}
static std::uint64_t input(ClientInstance* ci) {
    if(!panic && !inMenu && hitVirtual(localPlayer))return 1;
    return oldInput?oldInput(ci):0;
}
static void install() {
    const uintptr_t ztvs[]={0x101ff488,0x10202658,0x101ff908,0x101ff998,0x10200738,0x102007c8,0x101ff518,0x101fff18,0x101ff638,0x101ffab8};
    for(uintptr_t ztv:ztvs) {
        uintptr_t point=getAbsoluteAddress(MineLib,ztv+16),slot=point+2*sizeof(void*);if(!point)continue;
        auto prior=*reinterpret_cast<Handle*>(slot);void* replacement=reinterpret_cast<void*>(capture);
        auto* patch=new MemoryPatch(slot,&replacement,sizeof(replacement));vtables[hooks]=point;originals[hooks]=prior;++hooks;
        if(!patch->Modify()){--hooks;delete patch;continue;}
    }
    A64HookFunction(reinterpret_cast<void*>(getAbsoluteAddress(MineLib,0xccc075c)),reinterpret_cast<void*>(attack),reinterpret_cast<void**>(&oldAttack));
    A64HookFunction(reinterpret_cast<void*>(getAbsoluteAddress(MineLib,0x6462b60)),reinterpret_cast<void*>(input),reinterpret_cast<void**>(&oldInput));
    eclient_console::log.add("Fake Player hooks: packets %d, attack %s, input %s",hooks,oldAttack?"yes":"no",oldInput?"yes":"no");
}
static void removeNpc() {
    if(npcSent && callback.load()){auto p=create(0x9c1b274);if(p){std::memcpy(p.get()+48,&runtime,8);deliver(p);}}
    npcSent=false;npcActor.store(nullptr);equipKey=-1;
}
static void resetWorld() {
    std::lock_guard<std::mutex> guard(mutex);callback.store(nullptr);source.store(nullptr);npcActor.store(nullptr);
    enabled=npcSent=virtualMode=false;waitTicks=0;request.store(0);status.store(0);sim=State{};equipKey=-1;
}
static void spawnNpc() {
    runtime=0x0047617479000000ull+(++serial);auto p=create(0x9c1af18);if(!p)return;
    char* d=p.get();*reinterpret_cast<std::string*>(d+72)="EClientBot";
    std::uint64_t hi=0x45436c69656e7400ull ^ (serial*0x9e3779b97f4a7c15ull),lo=runtime;
    std::memcpy(d+96,&hi,8);std::memcpy(d+104,&lo,8);std::memcpy(d+112,&runtime,8);std::memcpy(d+120,&runtime,8);
    float xyz[]={sim.feet.x,sim.feet.y,sim.feet.z},velocity[]={0.f,0.f,0.f};std::memcpy(d+152,xyz,sizeof(xyz));std::memcpy(d+164,velocity,sizeof(velocity));
    std::memcpy(d+176,&sim.pitch,4);std::memcpy(d+180,&sim.yaw,4);std::memcpy(d+184,&sim.yaw,4);
    int platform=1,survival=0;std::memcpy(d+1688,&platform,4);std::memcpy(d+1692,&survival,4);
    deliver(p);npcSent=true;waitTicks=0;
    uintptr_t handler=*reinterpret_cast<uintptr_t*>(callback.load())-eclient_gatyo::base();
    eclient_console::log.add("Fake Player: AddPlayer sent, handler +0x%lx (%s)",(unsigned long)handler,handler==0x10046a10?"Legacy":"not Legacy");
}
static void moveNpc() {
    if(!npcSent || !npcActor.load() || !callback.load())return;
    auto p=create(0x9c1afc0);if(!p)return;char* d=p.get()+48;
    std::memcpy(d,&runtime,8);d[8]=sim.ground?1:0;
    float xyz[]={sim.feet.x,sim.feet.y+1.62f,sim.feet.z};std::memcpy(d+12,xyz,sizeof(xyz));
    auto angle=[](float v){return static_cast<unsigned char>(int(std::lround(v/360.f*256.f)));};
    d[24]=angle(sim.pitch);d[25]=d[26]=angle(sim.yaw);deliver(p);
}
static void equip(Actor* npc,const Settings& s) {
    int set=std::clamp(s.armor,0,3),key=set|((s.infinite||sim.totems>0)?4:0);if(key==equipKey)return;
    void* tls=eclient_gatyo::fn<void*(*)(void*)>(0xfe12cf0)(reinterpret_cast<void*>(eclient_gatyo::base()+0x106704b8));
    void* control=tls?reinterpret_cast<void**>(tls)[1]:nullptr;if(!control || *reinterpret_cast<long*>(reinterpret_cast<char*>(control)+8)<0)return;
    const char* gear[4][5]={{nullptr,nullptr,nullptr,nullptr,nullptr},{"minecraft:iron_helmet","minecraft:iron_chestplate","minecraft:iron_leggings","minecraft:iron_boots","minecraft:iron_sword"},{"minecraft:diamond_helmet","minecraft:diamond_chestplate","minecraft:diamond_leggings","minecraft:diamond_boots","minecraft:diamond_sword"},{"minecraft:netherite_helmet","minecraft:netherite_chestplate","minecraft:netherite_leggings","minecraft:netherite_boots","minecraft:netherite_sword"}};
    auto put=[&](const char* name,const auto& apply){
        void* empty=reinterpret_cast<void*>(eclient_gatyo::base()+0x10724d88);if(!name){apply(empty);return;}
        alignas(16) unsigned char stack[0xc0]{};
        eclient_gatyo::fn<void*(*)(void*,const char*,size_t,int,int,void*)>(0xd3df728)(stack,name,std::strlen(name),1,0,nullptr);
        apply(eclient_gatyo::fn<bool(*)(void*)>(0xd3e3d80)(stack)?empty:stack);
        reinterpret_cast<void(*)(void*)>((*reinterpret_cast<void***>(stack))[0])(stack);
    };
    auto vt=*reinterpret_cast<void***>(npc);
    for(int i=0;i<4;i++)put(gear[set][i],[&](void* stack){reinterpret_cast<void(*)(Actor*,int,void*)>(vt[584/8])(npc,i,stack);});
    auto pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(npc)+1392);
    if(pi)put(gear[set][4],[&](void* stack){eclient_gatyo::fn<void(*)(void*,int,void*,std::uint8_t,bool)>(0xce1128c)(pi,0,stack,0,false);});
    put((key&4)?"minecraft:totem_of_undying":nullptr,[&](void* stack){reinterpret_cast<bool(*)(Actor*,int,void*)>(vt[1368/8])(npc,1,stack);});
    equipKey=key;eclient_console::log.add("Fake Player: applied equipment set %d",set);
}
static void tick(LocalPlayer* player,Level* level,BlockSource* region,vec3_t* pos,const std::vector<Mob*>& actors) {
    if(!player || !level || !pos)return;std::lock_guard<std::mutex> guard(mutex);
    int req=request.exchange(0);Settings s=settings();
    if(req==2){removeNpc();enabled=virtualMode=false;status.store(0);eclient_console::log.add("Fake Player removed");return;}
    if(req==1){
        removeNpc();auto* rot=player->getRotation();float yaw=rot?rot->y:0,rad=yaw*.0174532925f;
        Vec feet{pos->x-std::sin(rad)*3,pos->y-1.62f,pos->z+std::cos(rad)*3};
        for(int i=0;i<24;i++){float y=std::floor(pos->y-.62f)-i;if(solid(region,feet.x,y,feet.z)){feet.y=y+1;break;}}
        sim.reset(feet,s);enabled=true;virtualMode=false;waitTicks=0;shownHp.store(20);shownTotems.store(sim.totems);
        eclient_console::log.add("Fake Player: spawn requested at %.1f %.1f %.1f",feet.x,feet.y,feet.z);
    }
    if(!enabled)return;
    if(req==4)sim.throwPearl({pos->x,pos->y,pos->z});
    if(req==3)notify(sim.damage(heldDamage(player),{pos->x,pos->y,pos->z},s,true));
    if(sim.dead){++sim.dead;if(sim.dead>80){removeNpc();sim.reset(sim.center,s);virtualMode=false;waitTicks=0;eclient_console::log.add("Fake Player: respawn");}}
    else {
        sim.healthTick(s);sim.pearlTick([&](float x,float y,float z){return solid(region,x,y,z);});sim.physics(s,[&](float x,float y,float z){return solid(region,x,y,z);});
        float dx=pos->x-sim.feet.x,dy=pos->y-(sim.feet.y+1.62f),dz=pos->z-sim.feet.z;
        sim.yaw=std::atan2(-dx,dz)*57.2957795f;sim.pitch=-std::atan2(dy,std::max(.01f,std::sqrt(dx*dx+dz*dz)))*57.2957795f;
        std::vector<Crystal> current;
        for(Mob* a:actors){if(!a||a->getEntityTypeId()!=71)continue;auto* p=a->getPosition();if(!p)continue;float x=p->x-sim.feet.x,z=p->z-sim.feet.z;if(x*x+z*z<256)current.push_back({reinterpret_cast<std::uintptr_t>(a),{p->x,p->y,p->z}});}
        for(const auto& old:sim.crystals){bool exists=false;for(const auto& c:current)if(c.id==old.id){exists=true;break;}if(!exists)notify(sim.explosion(old.pos,s,[&](float x,float y,float z){return solid(region,x,y,z);}));}
        sim.crystals.swap(current);
    }
    if(sim.dead>20 && npcSent)removeNpc();
    if(!virtualMode && sim.dead<=20) {
        if(!npcSent && callback.load() && oldAttack)spawnNpc();
        if(npcSent){using Find=Actor*(*)(Level*,std::uint64_t,bool);auto lookup=reinterpret_cast<Find>((*reinterpret_cast<void***>(level))[0x1e8/8]);Actor* found=lookup(level,runtime,false);
            if(found){if(!npcActor.exchange(found))eclient_console::log.add("Fake Player: NPC found");waitTicks=0;equip(found,s);moveNpc();}
            else {npcActor.store(nullptr);if(++waitTicks>20){removeNpc();virtualMode=true;eclient_console::log.add("Fake Player: NPC not created; virtual fallback");}}
        } else if(++waitTicks>100){virtualMode=true;eclient_console::log.add("Fake Player: no network handler; virtual fallback");}
    }
    shownHp.store(int(std::ceil(sim.health)));shownTotems.store(sim.totems);
    status.store(sim.dead?4:virtualMode?3:npcActor.load()?2:1);
}
static bool target(vec3_t& out) {std::lock_guard<std::mutex> guard(mutex);if(!enabled || sim.dead)return false;out={sim.feet.x,sim.feet.y+1.62f,sim.feet.z};return true;}
static void draw(ImDrawList* list,vec3_t camera,glm::vec2 fov,glm::mat4 matrix) {
    Vec feet;bool draw=false;int hp=0,tot=0;
    {std::lock_guard<std::mutex> guard(mutex);draw=enabled && virtualMode && sim.dead<=20;feet=sim.feet;hp=int(std::ceil(sim.health));tot=sim.totems;}
    if(!draw || !list || screenWidth<=0 || screenHeight<=0)return;
    glm::vec3 origin(camera.x,camera.y,camera.z);glm::vec2 head,foot;
    if(!OWorldToScreen(origin,glm::vec3(feet.x,feet.y+1.8f,feet.z),head,fov,glm::vec2(screenWidth,screenHeight),matrix) || !OWorldToScreen(origin,glm::vec3(feet.x,feet.y,feet.z),foot,fov,glm::vec2(screenWidth,screenHeight),matrix))return;
    float h=foot.y-head.y;if(!std::isfinite(h)||h<4||h>screenHeight*3)return;float x=(head.x+foot.x)*.5f,w=h*.30f;
    // World-anchored fallback mannequin. NPC mode uses Minecraft's actual model.
    auto rect=[&](float x1,float y1,float x2,float y2,ImU32 color){list->AddRectFilled(ImVec2(x+x1*w,head.y+y1*h),ImVec2(x+x2*w,head.y+y2*h),color);};
    rect(-.33f,0,.33f,.25f,IM_COL32(224,178,138,255));rect(-.40f,.25f,.40f,.66f,IM_COL32(50,168,165,255));
    rect(-.65f,.27f,-.40f,.65f,IM_COL32(224,178,138,255));rect(.40f,.27f,.65f,.65f,IM_COL32(224,178,138,255));
    rect(-.38f,.66f,-.02f,1,IM_COL32(55,62,137,255));rect(.02f,.66f,.38f,1,IM_COL32(55,62,137,255));
    char label[80];std::snprintf(label,sizeof(label),"EClientBot  %d HP  %d totems",hp,tot);auto size=ImGui::CalcTextSize(label);list->AddText(ImVec2(x-size.x*.5f,head.y-size.y-4),IM_COL32_WHITE,label);
}
static void ui() {
    if(ImGui::Button("Spawn / move here"))request.store(1);ImGui::SameLine();if(ImGui::Button("Remove"))request.store(2);ImGui::SameLine();if(ImGui::Button("Hit test"))request.store(3);
    if(ImGui::Button("Throw pearl toward me"))request.store(4);
    const char* labels[]={"off","waiting for NPC","NPC","virtual fallback","dead / respawning"};int st=status.load();ImGui::Text("%s | %d HP | %d totems",labels[std::clamp(st,0,4)],shownHp.load(),shownTotems.load());
    int a=armor.load();const char* sets[]={"None","Iron","Diamond","Netherite"};if(ImGui::Combo("Armor",&a,sets,4))armor.store(a);
    int p=protection.load();if(ImGui::SliderInt("Protection",&p,0,4))protection.store(p);
    int t=totemCount.load();if(ImGui::SliderInt("Totems on spawn",&t,0,20))totemCount.store(t);
    bool b=moving.load();if(ImGui::Checkbox("Walk around",&b))moving.store(b);
    b=infinite.load();if(ImGui::Checkbox("Infinite totems",&b))infinite.store(b);
    b=gapples.load();if(ImGui::Checkbox("Golden apples",&b))gapples.store(b);
    b=noKnock.load();if(ImGui::Checkbox("No knockback",&b))noKnock.store(b);
    ImGui::TextWrapped("NPC uses Minecraft's model. If creation fails, a world-anchored overlay dummy appears; aim and attack it. Crystal damage is simulated when a nearby crystal disappears.");
}
}
