#pragma once
#include <cstdint>
namespace eclient_use_state {
enum class Result { waiting, observed, timeout, cancelled };
struct Attempt {
    bool active=false; int slot=-1,previous=-1,count=0; long long started=0;
    void start(int s,int p,int n,long long t){active=true;slot=s;previous=p;count=n;started=t;}
    Result check(bool enabled,bool sameSlot,bool sameItem,int currentCount,long long now){
        if(!active)return Result::cancelled;
        if(!enabled || !sameSlot){active=false;return Result::cancelled;}
        if(!sameItem || currentCount<count){active=false;return Result::observed;}
        if(now-started>=3500){active=false;return Result::timeout;}
        return Result::waiting;
    }
};
}
