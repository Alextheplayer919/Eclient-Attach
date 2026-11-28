#pragma once
// GatoAuraX target/attack adapter and Gatyo's source rotation algorithms.
#include "gatyo_rotation.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
namespace eclient_gatyo_aura {
static std::atomic<bool> enabled{false},moveCamera{true},swingEnabled{true},mobs{false},hurtCheck{false};
static std::atomic<float> range{3.5f};
static std::atomic<int> interval{1},multiplier{1},rotationMode{GAX_ROT_NONE},targetMode{0};
static GaxRot rotation{};static float headYaw=0;static int tickCount=0;static bool running=false;
struct Published {bool active=false;float pitch=0,yaw=0,head=0;std::chrono::steady_clock::time_point time{};};
static std::mutex publishMutex;static Published published;
static GaxBox fakeBox{},npcBox{};static GaxV3 npcStep{};
alignas(16) static unsigned char fakeMemory[0x400]{};
struct StateVector {vec3_t pos,old,vel;};[[maybe_unused]] static StateVector fakeVector{};[[maybe_unused]] static vec2_t fakeRotation{};static bool fakeReady=false;
static Actor* fakeActor(){return fakeReady?reinterpret_cast<Actor*>(fakeMemory):nullptr;}
static Actor* npcActor=nullptr;
static bool GaxIsFake(Actor* a) {return a && a==fakeActor();}
static bool FakePlayerIsNpc(Actor* a) {return a && a==npcActor;}
static GaxV3 FakePlayerNpcStep(){return npcStep;}
static const GaxBox& FakePlayerNpcBox(){return npcBox;}
static const GaxBox& FakePlayerBox(){return fakeBox;}
static void publish(bool active) {
    std::lock_guard<std::mutex> guard(publishMutex);
    published={active,rotation.x,rotation.y,headYaw,std::chrono::steady_clock::now()};
}
static Published current() {
    std::lock_guard<std::mutex> guard(publishMutex);auto result=published;
    if(!enabled.load() || std::chrono::steady_clock::now()-result.time>std::chrono::milliseconds(500))result.active=false;
    return result;
}
static void reset(){publish(false);running=false;tickCount=0;fakeReady=false;npcActor=nullptr;g_gaxFsTargetYaw.clear();}
static void updateFake() {
    fakeReady=false;npcActor=nullptr;
#ifdef ECLIENT_COMBAT_LAB
    eclient_fake_sim::State state;bool virt=false;Actor* npc=nullptr;
    if(!eclient_fake::auraSnapshot(state,virt,npc))return;
    auto f=state.feet;fakeBox={{f.x-.3f,f.y,f.z-.3f},{f.x+.3f,f.y+1.8f,f.z+.3f}};
    npcBox=fakeBox;npcStep={state.velocity.x,state.velocity.y,state.velocity.z};npcActor=npc;
    if(!virt)return;
    vec3_t eyes{f.x,f.y+1.62f,f.z};fakeVector.old=fakeVector.pos;fakeVector.pos=eyes;
    fakeVector.vel={state.velocity.x,state.velocity.y,state.velocity.z};fakeRotation={state.pitch,state.yaw};
    *reinterpret_cast<StateVector**>(fakeMemory+Fields64::GetPosition)=&fakeVector;
    *reinterpret_cast<vec2_t**>(fakeMemory+Fields64::ActorRotation)=&fakeRotation;
    fakeReady=true;
#endif
}
static void tick(LocalPlayer* player,Level* level,GameMode* game,const std::vector<Mob*>& actors){
    if(!enabled.load() || panic || inMenu || !player || !level || !game || !GaxValid(player)){
#ifdef ECLIENT_COMBAT_LAB
        if(eclient_gatyo::cpvpActive())eclient_gatyo::cpvpTrack(0,{});
#endif
        if(running)reset();
        return;
    }
    if(!running){running=true;tickCount=0;auto* r=player->getRotation();if(r){rotation={r->x,r->y};headYaw=r->y;}}
    updateFake();GaxUpdateKeys(player);
    auto origin=GaxPos(player);const float radius=range.load();
    struct Candidate{Actor* actor;float dist;};std::vector<Candidate> candidates;
    auto add=[&](Actor* a){if(!a || !GaxValid(a))return;auto p=GaxPos(a);float x=p.x-origin.x,y=p.y-origin.y,z=p.z-origin.z;float d=x*x+y*y+z*z;
        if(std::isfinite(d)&&d<=radius*radius)candidates.push_back({a,d});};
    auto* manager=level->getGameplayUserManager();
    if(manager)manager->forEachActivePlayer([&](Player& other){
        if(&other==player || !GaxValid(&other))return true;
        const auto& name=other.getName();
        if(std::find(friendList.begin(),friendList.end(),name)!=friendList.end() || std::find(botList.begin(),botList.end(),name)!=botList.end())return true;
        if(!FakePlayerIsNpc(&other) && (other.getHealth()<=0 || !other.isAttackableGamemode()))return true;
        if(antiBot && !FakePlayerIsNpc(&other)) {auto* box=other.getAABBDim();if(!box || name.size()<2 || other.getNameTag().size()<2 || box->y<.05f || box->x<.05f || box->y>2.1f || box->x>10.9f)return true;}
        add(&other);return true;
    },true);
    bool fakeAllowed=std::find(friendList.begin(),friendList.end(),std::string("EClientBot"))==friendList.end() && std::find(botList.begin(),botList.end(),std::string("EClientBot"))==botList.end();
    if(fakeAllowed && npcActor && std::none_of(candidates.begin(),candidates.end(),[](const Candidate& c){return c.actor==npcActor;}))add(npcActor);
    if(fakeAllowed && fakeActor())add(fakeActor());
    bool targetMobs=mobs.load();
#ifdef ECLIENT_COMBAT_LAB
    if(eclient_gatyo::cpvpActive() && eclient_gatyo::cpvpMobs.load())targetMobs=true;
#endif
    if(targetMobs)for(Mob* mob:actors) {
        if(!mob || mob==player)continue;
        int type=mob->getEntityTypeId();
        // MobDetected's default accepts type 71; crystals are never aura targets.
        if(type==71 || type==319 || !MobDetected(type))continue;
        if(mob->getHealth()>0)add(mob);
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.dist<b.dist;});
    if(candidates.empty()){
#ifdef ECLIENT_COMBAT_LAB
        if(eclient_gatyo::cpvpActive())eclient_gatyo::cpvpTrack(0,{});
#endif
        publish(false);tickCount=0;return;
    }
#ifdef ECLIENT_COMBAT_LAB
    bool cpvpEnabled=eclient_gatyo::cpvpActive();
    if(cpvpEnabled){Actor* tracked=candidates.front().actor;auto target=GaxPos(tracked);auto box=GaxAABB(tracked);eclient_gatyo::cpvpTrack(reinterpret_cast<std::uintptr_t>(tracked),{target.x,box.lower.y+1.62f,target.z});
        if(!eclient_gatyo::cpvpNeedsHit()){publish(false);tickCount=0;return;}}
#else
    constexpr bool cpvpEnabled=false;
#endif
    if(++tickCount<std::max(1,interval.load()))return;
    Actor* target=candidates.front().actor;GaxV3 aim=GaxPos(target);aim.y+=.3f;
    float dx=aim.x-origin.x,dy=aim.y-origin.y,dz=aim.z-origin.z,dist=std::sqrt(dx*dx+dy*dy+dz*dz);
    int mode=std::clamp(rotationMode.load(),0,GAX_ROT_COUNT-1);
    if(mode!=GAX_ROT_NONE && mode!=GAX_ROT_STRAFE && dist>.001f){rotation={GaxWrap90(std::asin(std::clamp(dy/dist,-1.f,1.f))*-kGaxRad2Deg),GaxWrap180(-std::atan2(dx,dz)*kGaxRad2Deg)};headYaw=rotation.y;}
    GaxRotCtx ctx={50.f,.9f,8.f,1.f,3.f,.7f,1.5f,.1f,.1f,8,20,false,&rotation,&headYaw,1.f};
    switch(mode){
    case GAX_ROT_SMOOTH:GaxSmooth(ctx,target,player);break;case GAX_ROT_NEMESIS:GaxNemesis(ctx,target,player);break;
    case GAX_ROT_VORTEX:GaxVortex(ctx,target,player);break;case GAX_ROT_FRONTSTRAFE:GaxFrontStrafe(ctx,target,player);break;
    case GAX_ROT_AIRHVH:GaxAirHvHPro(ctx,target,player);break;case GAX_ROT_ATOMIC:GaxAtomic(ctx,target,player);break;
    case GAX_ROT_ALPHA:GaxAlpha(ctx,target,player);break;case GAX_ROT_SCORE:GaxScore(ctx,target,player);break;
    case GAX_ROT_LEGION:GaxLegion(ctx,target,player);break;case GAX_ROT_VALHALLA:GaxValhalla(ctx,target,player);break;
    case GAX_ROT_BERSERK:GaxBerserk(ctx,target,player);break;case GAX_ROT_RAGNAROK:GaxRagnarok(ctx,target,player);break;
    case GAX_ROT_FENRIR:GaxFenrir(ctx,target,player);break;case GAX_ROT_ECLIPSE:GaxEclipse(ctx,target,player);break;
    default:break;}
    bool valid=std::isfinite(rotation.x)&&std::isfinite(rotation.y)&&std::isfinite(headYaw);
    publish(valid && mode!=GAX_ROT_NONE && mode!=GAX_ROT_STRAFE);
    int n=(!cpvpEnabled && targetMode.load())?int(candidates.size()):1;
    for(int i=0;i<n;i++){Actor* actor=candidates[i].actor;
        if(hurtCheck.load() && !GaxIsFake(actor) && !FakePlayerIsNpc(actor) && static_cast<Mob*>(actor)->getHurtTime()>8)continue;
        for(int k=0;k<(cpvpEnabled?1:multiplier.load());k++){
#ifdef ECLIENT_COMBAT_LAB
            // Capture before attacking: an attack can kill/despawn the actor.
            GaxV3 hitPos{};std::uintptr_t hitKey=0;
            if(cpvpEnabled){auto p=GaxPos(actor);auto box=GaxAABB(actor);hitPos={p.x,box.lower.y+1.62f,p.z};hitKey=reinterpret_cast<std::uintptr_t>(actor);}
#endif
            if(swingEnabled.load())player->swing();
            if(GaxIsFake(actor)) {
#ifdef ECLIENT_COMBAT_LAB
                eclient_fake::auraHit(player);
#endif
            } else game->attack(actor,true);
#ifdef ECLIENT_COMBAT_LAB
            if(cpvpEnabled)eclient_gatyo::cpvpHit(hitKey,{hitPos.x,hitPos.y,hitPos.z});
#endif
        }
    }
    tickCount=0;
}
static void applyLocal(LocalPlayer* player){
    auto p=current();if(!p.active || !player)return;
    if(moveCamera.load()){player->setRotationX(p.pitch);player->setRotationY(p.yaw);}
    player->setYHeadRot(p.head);player->setYBodyRotation(p.head);
}
static void applyPacket(void* packet){
    auto p=current();if(!p.active || !packet)return;
    std::memcpy(reinterpret_cast<char*>(packet)+0x2c,&p.pitch,4);
    std::memcpy(reinterpret_cast<char*>(packet)+0x30,&p.yaw,4);
    std::memcpy(reinterpret_cast<char*>(packet)+0x40,&p.head,4);
}
static void ui(){
    bool b=enabled.load();if(ImGui::Checkbox("Gatyo KillAura",&b))enabled.store(b);
    int mode=rotationMode.load();if(ImGui::Combo("Rotation mode",&mode,kGaxRotNames,GAX_ROT_COUNT))rotationMode.store(mode);
    float r=range.load();if(ImGui::SliderFloat("Range##gax",&r,1.f,10.f,"%.1f blocks"))range.store(r);
    int n=interval.load();if(ImGui::SliderInt("Attack interval (ticks)",&n,1,40))interval.store(n);
    n=multiplier.load();if(ImGui::SliderInt("Hits per attack",&n,1,5))multiplier.store(n);
    int m=targetMode.load();const char* targets[]={"Nearest","Multiple"};if(ImGui::Combo("Targets",&m,targets,2))targetMode.store(m);
    b=moveCamera.load();if(ImGui::Checkbox("Move camera / target lock",&b))moveCamera.store(b);
    b=swingEnabled.load();if(ImGui::Checkbox("Swing##gax",&b))swingEnabled.store(b);
    b=hurtCheck.load();if(ImGui::Checkbox("Hurt check##gax",&b))hurtCheck.store(b);
    b=mobs.load();if(ImGui::Checkbox("Attack mobs##gax",&b))mobs.store(b);
    ImGui::TextWrapped("Use this instead of the original KillAura. Strafe, like Gatyo's source, does not apply packet rotations. Modes may flag or be rejected.");
}
}
