#pragma once
#include <algorithm>
#include <cstdint>
namespace eclient_combat_modes {
// Gatyo Offhand hysteresis: the lower boundary wins if boundaries coincide.
inline bool shieldLatch(bool previous,float health,float totemAt,float shieldAt) {
    if(health>=shieldAt)previous=true;
    if(health<=totemAt)previous=false;
    return previous;
}
enum class Phase { hit, base, waitBase, crystal, waitCrystal, waitBreak };
struct Cycle {
    Phase phase=Phase::base;std::uintptr_t target=0;int x=0,y=0,z=0,burst=0,tries=0;long long since=0,lastAction=0;
    void reset(){*this=Cycle{};}
    bool wantsHit()const{return phase==Phase::hit;}
    bool ready(long long now,int delay)const{return now>=lastAction && now-lastAction>=delay;}
    void attacked(std::uintptr_t id,long long now){if(phase==Phase::hit && id && id==target){phase=Phase::crystal;since=lastAction=now;tries=0;}}
    void track(std::uintptr_t id,long long now){if(id!=target){reset();target=id;since=lastAction=now;}}
    void placeBase(int bx,int by,int bz,long long now){x=bx;y=by;z=bz;phase=Phase::waitBase;since=lastAction=now;}
    void baseReady(long long now){phase=Phase::hit;since=lastAction=now;tries=0;}
    void crystalSent(long long now){phase=Phase::waitCrystal;since=lastAction=now;}
    void crystalFound(long long now){phase=Phase::waitBreak;since=now;tries=0;}
    void broken(long long now){++burst;tries=0;if(burst>=3)reset();else{phase=Phase::crystal;since=now;}}
    bool expired(long long now,long long timeout)const{return now-since>=timeout;}
};
}
