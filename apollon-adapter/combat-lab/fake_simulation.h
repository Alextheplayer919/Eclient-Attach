#pragma once
// Independent simulation adapted from Gatyo fakeplayer_gatyo.h.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace eclient_fake_sim {
struct Vec {float x=0,y=0,z=0;};
struct Crystal {std::uintptr_t id; Vec pos;};
struct Settings {int armor=2,protection=4,totems=2;bool infinite=false,move=false,noKnockback=false,gapples=true;};
struct State {
    Vec feet{},center{},velocity{}; float yaw=0,pitch=0,angle=0,health=20,absorption=0;
    int totems=2,hurt=0,dead=0,regen=0,regenTimer=0,absorptionTicks=0,natural=0,gapple=0;
    bool ground=false, pearlActive=false; Vec pearlPos{},pearlVelocity{}; int pearlTicks=0; std::vector<Crystal> crystals;
    void reset(const Vec& where,const Settings& settings) { *this=State{};feet=center=where;totems=settings.totems; }
    float reduce(float d,const Settings& s) const {
        const float armor[]={0,15,20,20},toughness[]={0,0,8,12};int a=std::clamp(s.armor,0,3);
        if(armor[a]) {float effective=std::clamp(armor[a]-d/(2+toughness[a]/4),armor[a]*.2f,20.f);d*=1-effective/25;d*=1-std::min(20,4*std::clamp(s.protection,0,4))*.04f;}
        return std::max(0.f,d);
    }
    // Returns 0 ignored, 1 hurt, 2 totem, 3 death.
    int damage(float raw,const Vec& from,const Settings& s,bool force=false,float knock=.4f) {
        if(dead || (!force && hurt>0))return 0;
        float amount=reduce(raw,s),absorbed=std::min(absorption,amount);absorption-=absorbed;health-=amount-absorbed;hurt=10;
        if(!s.noKnockback){float x=feet.x-from.x,z=feet.z-from.z,len=std::max(.01f,std::sqrt(x*x+z*z));velocity.x=velocity.x*.5f+x/len*knock;velocity.z=velocity.z*.5f+z/len*knock;if(ground)velocity.y=std::min(.4f,velocity.y*.5f+knock);}
        if(health>0)return 1;
        if(s.infinite || totems>0){if(!s.infinite)--totems;health=s.infinite?20.f:1.f;absorption=8;absorptionTicks=100;regen=800;return 2;}
        health=0;dead=1;return 3;
    }
    template<class Solid> float exposure(const Vec& center,Solid solid) const {
        int open=0,total=0;
        for(int ix=0;ix<=2;++ix)for(int iy=0;iy<=2;++iy)for(int iz=0;iz<=2;++iz){
            Vec p{feet.x-.3f+.3f*ix,feet.y+.9f*iy,feet.z-.3f+.3f*iz};float dx=p.x-center.x,dy=p.y-center.y,dz=p.z-center.z;
            int steps=std::max(1,int(std::sqrt(dx*dx+dy*dy+dz*dz)/.3f));bool blocked=false;
            for(int i=1;i<steps && !blocked;++i){float t=float(i)/steps;blocked=solid(center.x+dx*t,center.y+dy*t,center.z+dz*t);}
            open+=!blocked;++total;
        }return float(open)/total;
    }
    template<class Solid> int explosion(const Vec& pos,const Settings& settings,Solid solid) {
        float dx=feet.x-pos.x,dy=feet.y-pos.y,dz=feet.z-pos.z,distance=std::sqrt(dx*dx+dy*dy+dz*dz)/12;
        if(distance>1)return 0;
        float impact=(1-distance)*exposure(pos,solid);
        return damage(((impact*impact+impact)/2)*84+1,pos,settings,false,impact);
    }
    template<class Solid> void physics(const Settings& settings,Solid solid) {
        auto corners=[&](float x,float y,float z){return solid(x-.299f,y,z-.299f)||solid(x+.299f,y,z-.299f)||solid(x-.299f,y,z+.299f)||solid(x+.299f,y,z+.299f);};
        auto body=[&](float x,float y,float z){return corners(x,y+.1f,z)||corners(x,y+1,z)||corners(x,y+1.7f,z);};
        float dx=0,dz=0;if(settings.move){angle+=.035f;float x=center.x+std::cos(angle)*2.5f-feet.x,z=center.z+std::sin(angle)*2.5f-feet.z,len=std::sqrt(x*x+z*z);if(len>.25f){dx=x/len;dz=z/len;}}
        float a=ground?.1f:.02f;velocity.x+=dx*a;velocity.z+=dz*a;
        float ny=feet.y+velocity.y;
        if(velocity.y<=0 && corners(feet.x,ny,feet.z)){ny=std::floor(ny)+1;if(ny>feet.y+.6f)ny=feet.y;velocity.y=0;ground=true;}
        else if(velocity.y>0 && corners(feet.x,ny+1.8f,feet.z)){ny=feet.y;velocity.y=0;ground=false;}else ground=false;
        feet.y=ny;float nx=feet.x+velocity.x,nz=feet.z+velocity.z;
        if(body(nx,feet.y,feet.z)){if(ground&&(dx||dz))velocity.y=.42f;velocity.x=0;}else feet.x=nx;
        if(body(feet.x,feet.y,nz)){if(ground&&(dx||dz))velocity.y=.42f;velocity.z=0;}else feet.z=nz;
        float friction=ground?.546f:.91f;velocity.x*=friction;velocity.z*=friction;velocity.y=(velocity.y-.08f)*.98f;
    }
    void throwPearl(const Vec& target) {
        if(pearlActive || dead)return;
        pearlPos={feet.x,feet.y+1.62f,feet.z};
        float x=target.x-pearlPos.x,y=target.y-pearlPos.y,z=target.z-pearlPos.z;
        float len=std::max(.01f,std::sqrt(x*x+y*y+z*z));
        pearlVelocity={x/len*1.2f,y/len*1.2f+.25f,z/len*1.2f};pearlTicks=0;pearlActive=true;
    }
    template<class Solid> void pearlTick(Solid solid) {
        if(!pearlActive)return;
        Vec next{pearlPos.x+pearlVelocity.x,pearlPos.y+pearlVelocity.y,pearlPos.z+pearlVelocity.z},last=pearlPos;
        bool landed=++pearlTicks>200 || next.y< -80;
        for(int i=1;i<=4 && !landed;i++) {
            float t=float(i)/4;Vec sample{pearlPos.x+(next.x-pearlPos.x)*t,pearlPos.y+(next.y-pearlPos.y)*t,pearlPos.z+(next.z-pearlPos.z)*t};
            if(solid(sample.x,sample.y,sample.z))landed=true;else last=sample;
        }
        if(landed){pearlActive=false;feet=center={last.x,std::floor(last.y),last.z};velocity={};angle=0;return;}
        pearlPos=next;pearlVelocity.x*=.99f;pearlVelocity.y=pearlVelocity.y*.99f-.03f;pearlVelocity.z*=.99f;
    }
    void healthTick(const Settings& s){
        if(hurt>0)--hurt;
        if(absorptionTicks>0 && --absorptionTicks==0)absorption=0;
        if(gapple>0)--gapple;
        if(regen>0){--regen;if(++regenTimer>=25){regenTimer=0;health=std::min(20.f,health+1);}}
        else if(health<20 && ++natural>=80){natural=0;health=std::min(20.f,health+1);}
        if(s.gapples && health<=10 && gapple==0){absorption=std::max(absorption,4.f);absorptionTicks=2400;regen=std::max(regen,100);gapple=120;}
    }
};
}
