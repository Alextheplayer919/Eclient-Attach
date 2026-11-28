#pragma once
// Experimental GameMode::useItem candidate. 1.21.111 ARM64 ONLY. ABI and
// consumption behavior are NOT verified; the toggle defaults off.
#include "use_state.h"
namespace eclient_autouse {
static std::atomic<bool> enabled{false},autoApple{false},autoStrength{false};
static std::atomic<int> request{0},appleAt{12},strengthInterval{90};
static eclient_use_state::Attempt attempt;
static int activeKind=0;static bool useSent=false;static long long lastApple=0,lastStrength=0,retryAfter=0;
static bool failed=false;
static bool busy(){return attempt.active;}
static const char* itemName(void* stack){
    if(!stack || eclient_gatyo::fn<bool(*)(void*)>(0xd3e3d80)(stack))return "";
    void** weak=*reinterpret_cast<void***>(static_cast<char*>(stack)+8);
    char* item=weak?static_cast<char*>(*weak):nullptr;if(!item)return "";
    const auto& full=*reinterpret_cast<const std::string*>(item+0xf0);
    auto cut=full.find(':');return full.c_str()+(cut==std::string::npos?0:cut+1);
}
static bool matches(void* stack,int kind){
    if(kind==1)return eclient_gatyo::itemIs(stack,0x1071f678);
    if(kind==2){std::int16_t aux=0;if(!stack)return false;std::memcpy(&aux,static_cast<char*>(stack)+0x20,2);return std::strcmp(itemName(stack),"potion")==0 && aux==33;}
    return false;
}
static int count(void* stack){return stack?*(reinterpret_cast<unsigned char*>(stack)+0x22):0;}
static void resetWorld(){attempt={};request.store(0);activeKind=0;useSent=false;lastApple=lastStrength=retryAfter=0;failed=false;}
static void finish(LocalPlayer* player,GameMode* game,void* ci,void* container,int current,eclient_use_state::Result result){
    if(useSent && result!=eclient_use_state::Result::observed && current==attempt.slot) {
        eclient_console::log.add("Auto Use: attempting release after cancellation/timeout");
        eclient_gatyo::fn<void(*)(void*)>(0xccc4c38)(game);
    }
    // Do not override a manual hotbar change, and never retain a game pointer.
    if(current==attempt.slot && attempt.previous>=0 && attempt.previous<9 && current!=attempt.previous){
        auto* pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(player)+1392);
        *reinterpret_cast<int*>(pi+16)=attempt.previous;eclient_gatyo::sendEquipment(player,ci,container,attempt.previous);
    }
    auto now=eclient_gatyo::now();
    if(result==eclient_use_state::Result::observed){
        if(activeKind==1)lastApple=now;else lastStrength=now;
        eclient_console::log.add("Auto Use: item changed; consumption likely, effect not verified");
    }else if(result==eclient_use_state::Result::timeout){failed=true;eclient_console::log.add("Auto Use: no consumption observed; stopped, re-enable to retry");}
    else eclient_console::log.add("Auto Use: cancelled or slot changed");
    retryAfter=now+2000;activeKind=0;useSent=false;
}
static void tick(LocalPlayer* player,GameMode* game,void* ci){
    if(!player || !game || !eclient_gatyo::base()){resetWorld();return;}
    void* container=eclient_gatyo::inventory(player);auto* pi=*reinterpret_cast<char**>(reinterpret_cast<char*>(player)+1392);
    if(!container || !pi){resetWorld();return;}
    int selected=*reinterpret_cast<int*>(pi+16);if(selected<0 || selected>=9){resetWorld();return;}
    long long t=eclient_gatyo::now();
    if(attempt.active){
        void* stack=eclient_gatyo::slot(container,attempt.slot);
        if(enabled.load() && selected==attempt.slot && !useSent && matches(stack,activeKind) && count(stack)==attempt.count){
            useSent=true;
            eclient_console::log.add("Auto Use: attempting item use; experimental call");
            eclient_gatyo::fn<void(*)(void*,void*,int)>(0xccc4438)(game,stack,0);
            return;
        }
        auto result=attempt.check(enabled.load(),selected==attempt.slot,matches(stack,activeKind),count(stack),t);
        if(result!=eclient_use_state::Result::waiting)finish(player,game,ci,container,selected,result);
        return;
    }
    if(!enabled.load()){failed=false;request.store(0);return;}
    if(failed || t<retryAfter)return;
    int kind=request.exchange(0);
    if(!kind && autoApple.load() && player->getHealth()<=appleAt.load() && (!lastApple || t-lastApple>=30000))kind=1;
    if(!kind && autoStrength.load() && (!lastStrength || t-lastStrength>=strengthInterval.load()*1000LL))kind=2;
    if(!kind)return;
    int chosen=-1;for(int i=0;i<9;++i){auto* stack=eclient_gatyo::slot(container,i);if(matches(stack,kind) && count(stack)>0){chosen=i;break;}}
    if(chosen<0){eclient_console::log.add("Auto Use: %s missing in hotbar",kind==1?"enchanted apple":"Strength II potion (aux 33)");retryAfter=t+5000;return;}
    auto* stack=eclient_gatyo::slot(container,chosen);int before=count(stack);
    attempt.start(chosen,selected,before,t);activeKind=kind;useSent=false;
    if(chosen!=selected){*reinterpret_cast<int*>(pi+16)=chosen;eclient_gatyo::sendEquipment(player,ci,container,chosen);}
    eclient_console::log.add("Auto Use: attempting %s in slot %d; experimental call",kind==1?"enchanted apple":"Strength II potion",chosen);
}
static void ui(){
    bool on=enabled.load();if(ImGui::Checkbox("Experimental Auto Use",&on)){enabled.store(on);if(!on)request.store(0);}
    ImGui::TextWrapped("Experimental native call may crash or do nothing. Keep items in hotbar; disable other item-switch modules. No effect detection yet.");
    bool a=autoApple.load();if(ImGui::Checkbox("Auto enchanted apple",&a))autoApple.store(a);
    int hp=appleAt.load();if(ImGui::SliderInt("Apple at or below HP",&hp,1,20))appleAt.store(hp);
    bool p=autoStrength.load();if(ImGui::Checkbox("Auto Strength II potion",&p))autoStrength.store(p);
    int sec=strengthInterval.load();if(ImGui::SliderInt("Potion interval (seconds)",&sec,30,300))strengthInterval.store(sec);
    if(ImGui::Button("Try enchanted apple"))request.store(1);
    if(ImGui::Button("Try Strength II potion"))request.store(2);
    ImGui::TextWrapped("Enable first. Timeout stops retries until disabled and re-enabled; see Console.");
}
}
