#pragma once
// Minimal client-only NPC port based on Gatyo netinject/fakeplayer. Packet
// handlers remain game-owned; capture is reset when the world is left.
#include <atomic>
#include <memory>
#include <string>
namespace eclient_fake {
using Handle=void(*)(void*,void*,void*,void*);
using Packet=std::shared_ptr<char>;
static std::atomic<void*> callback{nullptr},source{nullptr},npcActor{nullptr};
static std::atomic<int> request{0},health{20},totems{2};
static std::atomic<bool> spawned{false};
static uintptr_t vtables[10]{};static Handle originals[10]{};static int hooks=0;
static std::uint64_t serial=0,runtime=0;
static bool (*oldAttack)(GameMode*,Actor*,bool)=nullptr;
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
    if(!callback.load())return;
    auto p=create(0x9c1b9b8);if(!p)return;
    std::memcpy(p.get()+48,&runtime,8);p.get()[56]=static_cast<char>(id);
    int zero=0;std::memcpy(p.get()+60,&zero,4);deliver(p);
}
static bool attack(GameMode* gm,Actor* target,bool arg) {
    if(target && target==npcActor.load(std::memory_order_acquire)) {
        int hp=health.fetch_sub(4)-4;
        if(hp<=0) {
            int left=totems.load();
            if(left>0){totems.fetch_sub(1);health.store(20);event(65);eclient_console::log.add("Fake player popped a totem (%d left)",left-1);}
            else {health.store(20);event(3);eclient_console::log.add("Fake player defeated; reset health");}
        } else {event(2);eclient_console::log.add("Fake player hit: %d simulated HP",hp);}
        return true;
    }
    return oldAttack(gm,target,arg);
}
static void install() {
    const uintptr_t ztvs[]={0x101ff488,0x10202658,0x101ff908,0x101ff998,0x10200738,0x102007c8,0x101ff518,0x101fff18,0x101ff638,0x101ffab8};
    for(uintptr_t ztv:ztvs) {
        uintptr_t point=getAbsoluteAddress(MineLib,ztv+16),slot=point+2*sizeof(void*);
        if(!point)continue;
        auto prior=*reinterpret_cast<Handle*>(slot);void* replacement=reinterpret_cast<void*>(capture);
        // Keep patches alive so the vtable stays patched for the process lifetime.
        auto* patch=new MemoryPatch(slot,&replacement,sizeof(replacement));
        vtables[hooks]=point;originals[hooks]=prior;++hooks;
        if(!patch->Modify()){--hooks;delete patch;continue;}
    }
    auto address=getAbsoluteAddress(MineLib,0xccc075c);
    if(address) A64HookFunction(reinterpret_cast<void*>(address),reinterpret_cast<void*>(attack),reinterpret_cast<void**>(&oldAttack));
    eclient_console::log.add("Fake player: %d packet capture hooks, attack hook %s",hooks,oldAttack?"installed":"unavailable");
}
static void resetWorld() {
    callback.store(nullptr);source.store(nullptr);npcActor.store(nullptr);spawned.store(false);request.store(0);
}
static void remove() {
    if(spawned.load() && callback.load()) {
        auto p=create(0x9c1b274);if(p){std::memcpy(p.get()+48,&runtime,8);deliver(p);}
        eclient_console::log.add("Fake player: remove requested");
    }
    spawned.store(false);npcActor.store(nullptr);
}
static void tick(LocalPlayer* player,Level* level,vec3_t* pos) {
    if(!player || !level || !pos)return;
    int req=request.exchange(0);
    if(req==2){remove();return;}
    if(req==1) {
        if(!callback.load()){eclient_console::log.add("Fake player: waiting for a game packet; try again shortly");return;}
        if(!oldAttack){eclient_console::log.add("Fake player: attack interceptor unavailable");return;}
        remove();runtime=0x0047617479000000ull+(++serial);
        auto p=create(0x9c1af18);if(!p){eclient_console::log.add("Fake player: AddPlayer creation failed");return;}
        char* d=p.get();*reinterpret_cast<std::string*>(d+72)="EClientBot";
        std::uint64_t hi=0x45436c69656e7400ull,lo=runtime;
        std::memcpy(d+96,&hi,8);std::memcpy(d+104,&lo,8);std::memcpy(d+112,&runtime,8);std::memcpy(d+120,&runtime,8);
        auto* rot=player->getRotation();float yaw=rot?rot->y:0.0f,rad=yaw*0.0174532925f;
        float xyz[3]={pos->x-std::sin(rad)*3.0f,pos->y-1.62f,pos->z+std::cos(rad)*3.0f};
        std::memcpy(d+152,xyz,sizeof(xyz));float zero=0;std::memcpy(d+176,&zero,4);std::memcpy(d+180,&yaw,4);std::memcpy(d+184,&yaw,4);
        int platform=1,survival=0;std::memcpy(d+1688,&platform,4);std::memcpy(d+1692,&survival,4);
        health.store(20);totems.store(2);deliver(p);spawned.store(true);
        eclient_console::log.add("Fake player: client AddPlayer delivered; checking actor");
    }
    if(spawned.load()) {
        using Find=Actor*(*)(Level*,std::uint64_t,bool);
        auto lookup=reinterpret_cast<Find>((*reinterpret_cast<void***>(level))[0x1e8/8]);
        Actor* found=lookup(level,runtime,false);
        if(found && !npcActor.exchange(found)) eclient_console::log.add("Fake player actor found in client world");
        else if(!found)npcActor.store(nullptr);
    }
}
static void ui() {
    ImGui::TextWrapped("Experimental client-only test player: stationary, 20 simulated HP and two simulated totems. No armor, movement, or explosion simulation yet.");
    if(ImGui::Button("Spawn fake player"))request.store(1);
    ImGui::SameLine();if(ImGui::Button("Remove fake player"))request.store(2);
    ImGui::Text("Network: %s | actor: %s | HP %d | totems %d",callback.load()?"ready":"waiting",npcActor.load()?"present":"not found",health.load(),totems.load());
}
}
