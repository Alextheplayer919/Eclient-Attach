#pragma once
// Experimental GameMode::useItem candidate. 1.21.111 ARM64 ONLY. ABI and
// consumption behavior are NOT verified; the toggle defaults off.
#include "use_state.h"
namespace eclient_autouse {
static std::atomic<bool> enabled{false},autoApple{false},autoStrength{false},traceUse{false},inspectHotbar{false};
static std::atomic<int> request{0},appleAt{12},strengthInterval{90};
static std::atomic<bool> holdRelease{true};
static eclient_use_state::Attempt attempt;
static int activeKind=0;static bool useSent=false;static long long lastApple=0,lastStrength=0,retryAfter=0;
static bool failed=false;static long long nextTrace=0;static int lastDuration=-9999,lastTraceSlot=-1;
// Opt-in instrumentation only. The hook stays installed until the process exits,
// but logs only while tracing. Compare pointers without dereferencing callback args.
#if defined(__aarch64__)
using UseItemFn=bool(*)(void*,void*);
using ReleaseFn=void(*)(void*);
static ReleaseFn originalRelease=nullptr;static bool releaseHookTried=false;
static std::atomic<bool> blockRelease{false};
static std::atomic<void*> heldGame{nullptr};static std::atomic<unsigned> suppressedReleases{0};
static void interceptedRelease(void* game){
    if(blockRelease.load() && game==heldGame.load()) {suppressedReleases.fetch_add(1);return;}
    originalRelease(game);
}
static bool ensureReleaseHook(){
    if(!releaseHookTried){
        releaseHookTried=true;
        A64HookFunction(reinterpret_cast<void*>(eclient_gatyo::base()+0xccc4c38),reinterpret_cast<void*>(&interceptedRelease),reinterpret_cast<void**>(&originalRelease));
        eclient_console::log.add("Auto Use: release hold hook %s",originalRelease?"installed":"failed");
    }
    return originalRelease!=nullptr;
}
static UseItemFn originalUseItem=nullptr;static bool traceHookTried=false;
static std::atomic<void*> traceGame{nullptr},traceHeld{nullptr};
static thread_local bool internalUse=false;
static bool tracedUseItem(void* game,void* stack){
    bool result=originalUseItem(game,stack);
    if(traceUse.load()) eclient_console::log.add("Use hook: %s result=%d game=%d held=%d",internalUse?"auto":"manual/game",result?1:0,game==traceGame.load()?1:0,stack==traceHeld.load()?1:0);
    return result;
}
static void ensureTraceHook(){
    if(traceHookTried)return;
    traceHookTried=true;
    A64HookFunction(reinterpret_cast<void*>(eclient_gatyo::base()+0xccc4438),reinterpret_cast<void*>(&tracedUseItem),reinterpret_cast<void**>(&originalUseItem));
    eclient_console::log.add("Use trace: useItem hook %s",originalUseItem?"installed":"failed");
}
#endif
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
static void resetWorld(){
#if defined(__aarch64__)
    blockRelease.store(false);heldGame.store(nullptr);traceGame.store(nullptr);traceHeld.store(nullptr);
#endif
    attempt={};request.store(0);activeKind=0;useSent=false;lastApple=lastStrength=retryAfter=nextTrace=0;failed=false;lastDuration=-9999;lastTraceSlot=-1;}
static void finish(LocalPlayer* player,GameMode* game,void* ci,void* container,int current,eclient_use_state::Result result){
#if defined(__aarch64__)
    blockRelease.store(false);heldGame.store(nullptr);
    unsigned blocked=suppressedReleases.exchange(0);
    if(blocked)eclient_console::log.add("Auto Use: suppressed %u early release calls",blocked);
#endif
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
#if defined(__aarch64__)
    traceGame.store(game);traceHeld.store(eclient_gatyo::slot(container,selected));
    if(traceUse.load())ensureTraceHook();
#endif
    if(inspectHotbar.exchange(false)) {
        eclient_console::log.add("Auto Use: hotbar inspection (slot, name, aux, count)");
        for(int i=0;i<9;i++) {
            void* stack=eclient_gatyo::slot(container,i);const char* name=itemName(stack);
            if(!*name)continue;
            std::int16_t aux=0;std::memcpy(&aux,static_cast<char*>(stack)+0x20,2);
            eclient_console::log.add("slot %d: %.45s aux=%d count=%d",i,name,int(aux),count(stack));
        }
    }
    if(traceUse.load() && t>=nextTrace) {
        nextTrace=t+300;void** vt=*reinterpret_cast<void***>(player);
        void* candidate=vt[151];void* expected=reinterpret_cast<void*>(eclient_gatyo::base()+0xce0af1c);
        if(candidate==expected){
            int duration=reinterpret_cast<int(*)(void*)>(candidate)(player);
            if(duration!=lastDuration || selected!=lastTraceSlot){
                void* held=eclient_gatyo::slot(container,selected);
                eclient_console::log.add("Use trace: slot=%d item=%.30s duration=%d auto=%d",selected,itemName(held),duration,attempt.active?1:0);
                lastDuration=duration;lastTraceSlot=selected;
            }
        } else if(lastDuration!=-9998){eclient_console::log.add("Use trace: player vtable differs; duration unavailable");lastDuration=-9998;}
    }
    if(!traceUse.load()){lastDuration=-9999;lastTraceSlot=-1;}
    if(attempt.active){
        void* stack=eclient_gatyo::slot(container,attempt.slot);
        if(enabled.load() && selected==attempt.slot && !useSent && matches(stack,activeKind) && count(stack)==attempt.count){
#if defined(__aarch64__)
            if(holdRelease.load()){
                if(!ensureReleaseHook()){attempt.active=false;failed=true;finish(player,game,ci,container,selected,eclient_use_state::Result::cancelled);return;}
                suppressedReleases.store(0);heldGame.store(game);blockRelease.store(true);
            }
#endif
            useSent=true;
            eclient_console::log.add("Auto Use: attempting item use; experimental call");
            // October 2025 Bedrock header suggests bool GameMode::useItem(ItemStack&),
            // but target ABI still needs confirmation. A true result is not consumption.
#if defined(__aarch64__)
            internalUse=true;
#endif
            bool result=eclient_gatyo::fn<bool(*)(void*,void*)>(0xccc4438)(game,stack);
#if defined(__aarch64__)
            internalUse=false;
#endif
            eclient_console::log.add("Auto Use: candidate returned %d (not confirmation)",result?1:0);
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
    ImGui::TextWrapped("Experimental use and release hooks may crash or do nothing. Keep items in hotbar; disable other item-switch modules. No effect detection yet.");
    bool hold=holdRelease.load();if(ImGui::Checkbox("Hold use (experimental release hook)",&hold))holdRelease.store(hold);
    bool a=autoApple.load();if(ImGui::Checkbox("Auto enchanted apple",&a))autoApple.store(a);
    int hp=appleAt.load();if(ImGui::SliderInt("Apple at or below HP",&hp,1,20))appleAt.store(hp);
    bool p=autoStrength.load();if(ImGui::Checkbox("Auto Strength II potion",&p))autoStrength.store(p);
    int sec=strengthInterval.load();if(ImGui::SliderInt("Potion interval (seconds)",&sec,30,300))strengthInterval.store(sec);
    bool trace=traceUse.load();if(ImGui::Checkbox("Trace manual and automatic use",&trace))traceUse.store(trace);
    if(ImGui::Button("Log hotbar items"))inspectHotbar.store(true);
    if(ImGui::Button("Try enchanted apple"))request.store(1);
    if(ImGui::Button("Try Strength II potion"))request.store(2);
    ImGui::TextWrapped("Trace installs an experimental hook until restart. Enable first. Timeout stops retries until disabled and re-enabled; see Console.");
}
}
