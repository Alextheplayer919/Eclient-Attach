#include "modules/GameplayModules.h"
#include "gui/clickgui/eclient_vars.h"
#include "profile/Offsets_1_21_111.h"
#include "memory/NativeMemory.h"
#include "dobby.h"
#include "modules/flyx/flyx_core.h"
#include <android/log.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <time.h>
#include <atomic>

extern "C" float spiral_flyx_sin(float a) { return sinf(a); }
extern "C" float spiral_flyx_cos(float a) { return cosf(a); }
extern "C" float spiral_flyx_sqrt(float v) { return sqrtf(v); }

namespace eclient_runtime::modules::gameplay {
namespace {
constexpr const char* TAG = "EClientGameplay";
using namespace eclient_runtime::profile::mc_1_21_111;

using NormalTickFn = void* (*)(void*);
using SpeedFn = void (*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*,void*);
using GetAbilitiesFn = void* (*)(void*);
using GetLayerFn = void* (*)(void*, int);
using SetAbilityFn = void (*)(void*, int, bool);
using GetRuntimeActorFn = std::vector<void*> (*)(void*);
using AttackFn = void (*)(void*, void*, bool);
using IsAttackableFn = bool (*)(void*);
using GetHealthFn = int (*)(void*);
using LerpMotionFn = void (*)(void*, const FlyXVec3*);
using SwingFn = void (*)(void*);

NormalTickFn oldNormalTick = nullptr;
SpeedFn oldSpeed = nullptr;
GetAbilitiesFn getAbilities = nullptr;
GetLayerFn getLayer = nullptr;
SetAbilityFn setAbility = nullptr;
GetRuntimeActorFn getRuntimeActors = nullptr;
AttackFn attack = nullptr;
IsAttackableFn isAttackable = nullptr;
GetHealthFn getHealth = nullptr;
LerpMotionFn lerpMotion = nullptr;
SwingFn swingFn = nullptr;
std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }

// ── FlyX ───────────────────────────────────────────────────────────────────
static FlyXCore g_flyXCore{};
static bool g_flyXInitialized = false;
static uint64_t g_lastTickNs = 0;
static float g_upHFactor = 1.0f;
static float g_upVFactor = 1.0f;

static uint64_t clockNowNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uintptr_t enttComponent(const void* player, uint32_t hash, unsigned stride) {
    uintptr_t actor = (uintptr_t)player;
    uintptr_t registry = *(uintptr_t*)(actor + 0x10);
    if (registry < 4096 || (registry & 7)) return 0;
    uintptr_t buckets = *(uintptr_t*)(registry + 0x38);
    uintptr_t bucketEnd = *(uintptr_t*)(registry + 0x40);
    uintptr_t nodes = *(uintptr_t*)(registry + 0x50);
    uintptr_t nodeEnd = *(uintptr_t*)(registry + 0x58);
    if (buckets < 4096 || (buckets & 7) || bucketEnd < buckets) return 0;
    if (nodes < 4096 || (nodes & 7) || nodeEnd < nodes) return 0;
    uintptr_t bucketCount = (bucketEnd - buckets) / 8;
    uintptr_t nodeCount = (nodeEnd - nodes) / 32;
    if (!bucketCount || (bucketCount & (bucketCount-1)) || !nodeCount) return 0;
    if (bucketCount > (1u<<20) || nodeCount > (1u<<20)) return 0;
    uintptr_t idx = *(uintptr_t*)(buckets + (hash & (bucketCount-1)) * 8);
    uintptr_t pool = 0;
    for (unsigned tries=0; tries<64 && idx < nodeCount; ++tries) {
        uintptr_t node = nodes + idx*32;
        if (*(uint32_t*)(node+8) == hash) { pool = *(uintptr_t*)(node+0x10); break; }
        idx = *(uintptr_t*)(node+0);
    }
    if (pool < 4096 || (pool & 7)) return 0;
    uint32_t entity = *(uint32_t*)(actor + 0x18);
    uintptr_t sparse = *(uintptr_t*)(pool+8);
    uintptr_t sparseEnd = *(uintptr_t*)(pool+0x10);
    if (sparse < 4096 || (sparse & 7) || sparseEnd < sparse) return 0;
    uintptr_t sparseCount = (sparseEnd - sparse)/8;
    if (sparseCount > 128) return 0;
    unsigned sparsePage = (entity >> 11) & 127;
    if (sparsePage >= sparseCount) return 0;
    uintptr_t sparseEntries = *(uintptr_t*)(sparse + sparsePage*8);
    if (sparseEntries < 4096 || (sparseEntries & 7)) return 0;
    uint32_t packed = *(uint32_t*)(sparseEntries + (entity & 2047)*4);
    if ((packed ^ (entity & 0xfffc0000u)) >= 0x3ffffu) return 0;
    if (!stride) return 1;
    uintptr_t pages = *(uintptr_t*)(pool+0x50);
    uintptr_t pagesEnd = *(uintptr_t*)(pool+0x58);
    if (pages < 4096 || (pages & 7) || pagesEnd < pages) return 0;
    uintptr_t pageCount = (pagesEnd - pages)/8;
    if (pageCount > 2048) return 0;
    unsigned page = (packed & 0x3ffffu) >> 7;
    if (page >= pageCount) return 0;
    uintptr_t data = *(uintptr_t*)(pages + page*8);
    if (data < 4096 || (data & 7)) return 0;
    return data + (packed & 127)*stride;
}
static bool buildFlyXInput(void* player, FlyXInput* out) {
    uintptr_t actor = (uintptr_t)player;
    uintptr_t move = enttComponent(player, MoveInputComponentHash, 160);
    if (!move) return false;
    float strafe = 0, forward = 0;
    bool hasAxes = false;
    auto axesOk = [](float x,float y){ return std::isfinite(x)&&std::isfinite(y)&&fabsf(x)<=1.001f&&fabsf(y)<=1.001f&&(fabsf(x)>0.04f||fabsf(y)>0.04f); };
    float x = *(float*)(move+0x58), y = *(float*)(move+0x58+4);
    if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; }
    if (!hasAxes) { x = *(float*)(move+0x3c); y = *(float*)(move+0x40); if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; } }
    if (!hasAxes) { x = *(float*)(move+0x14); y = *(float*)(move+0x14+4); if (axesOk(x,y)) { strafe=x; forward=y; hasAxes=true; } }
    if (!hasAxes) {
        uint8_t fwd = *(uint8_t*)(move+0x0d), back = *(uint8_t*)(move+0x0e);
        uint8_t left = *(uint8_t*)(move+0x0f), right = *(uint8_t*)(move+0x10);
        if (fwd>1||back>1||left>1||right>1) return false;
        strafe = (float)((int)left - (int)right);
        forward = (float)((int)fwd - (int)back);
    }
    out->strafe = strafe; out->forward = forward;
    uint8_t jump = *(uint8_t*)(move+0x98), sneak = *(uint8_t*)(move+0x94), inWaterFlag = *(uint8_t*)(move+0x99);
    if (jump>1||sneak>1||inWaterFlag>1) return false;
    out->jump = jump; out->sneak = sneak;
    out->touching_terrain = inWaterFlag || (enttComponent(player, 0xd5ee0bd8u, 0)!=0);
    uintptr_t posPtr = *(uintptr_t*)(actor + GetPositionField);
    uintptr_t rotPtr = *(uintptr_t*)(actor + RotationPtrField);
    if (posPtr < 4096 || (posPtr & 3) || rotPtr < 4096 || (rotPtr & 7)) return false;
    out->position_x = *(float*)(posPtr + 0);
    out->position_z = *(float*)(posPtr + 8);
    out->yaw_degrees = *(float*)(rotPtr + 4);
    if (!std::isfinite(out->position_x) || !std::isfinite(out->position_z) || !std::isfinite(out->yaw_degrees)) return false;
    if (!std::isfinite(out->strafe) || !std::isfinite(out->forward)) return false;
    return true;
}

// ── KillAura — source of truth is Zeff variables.h (killRange etc.) ─────────
static int   g_currentKillTick = 1;
static bool  g_canKillAuraDamage = true;
static float g_auraAngleX = 0.f, g_auraAngleY = 0.f; // pitch, yaw

constexpr float PI = 3.14159265358979323846f;
static inline float deg2rad(float d){ return d * (PI/180.f); }
static inline float rad2deg(float r){ return r * (180.f/PI); }

static bool canBeHurted(void* actor){
    // Apollon: *(int*)(actor + HurtTime) < 2
    int hurt = *(int*)((uintptr_t)actor + HurtTimeField);
    return hurt < 2;
}
static float hackDistanceTo(void* aPosPtr, void* bPosPtr){
    float ax = *(float*)((uintptr_t)aPosPtr+0), ay = *(float*)((uintptr_t)aPosPtr+4), az = *(float*)((uintptr_t)aPosPtr+8);
    float bx = *(float*)((uintptr_t)bPosPtr+0), by = *(float*)((uintptr_t)bPosPtr+4), bz = *(float*)((uintptr_t)bPosPtr+8);
    float dx=ax-bx, dy=ay-by, dz=az-bz;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}
static void calcPlayerAngle(const float px,float py,float pz, const float tx,float ty,float tz, float &outPitch, float &outYaw){
    // Apollon Vector.h: CalcPlayerAngle — eye-height aware
    float dx = tx - px, dy = ty - py, dz = tz - pz;
    // emulate: if (dst.y -0.6 > y) diff.y = dst.y - (y+1)
    if (ty - 0.6f > py) dy = ty - (py + 1.0f);
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) { outPitch=0; outYaw=0; return; }
    float ny = dy / len;
    if (ny > 1.f) ny=1.f; if (ny < -1.f) ny=-1.f;
    outPitch = asinf(ny) * -57.2957795f;
    outYaw   = -atan2f(dx, dz) * 57.2957795f;
}
static void applyAuraAngle(void* player){
    uintptr_t rotPtr = *(uintptr_t*)((uintptr_t)player + RotationPtrField);
    if (rotPtr < 4096) return;
    float pitch = g_auraAngleX, yaw = g_auraAngleY;
    while (yaw > 180.f) yaw-=360.f; while (yaw < -180.f) yaw+=360.f;
    if (pitch>90.f) pitch=90.f; if(pitch<-90.f) pitch=-90.f;
    // Apollon Intersects: rotations -> setRotationX(pitch) + body/head yaw, targetLock -> setRotationY(yaw)
    if (rotations){
        *(float*)(rotPtr + 0) = pitch;
        if (base){
            auto setBody = reinterpret_cast<void(*)(void*,float)>(fn(SetYBodyRot));
            auto setHead = reinterpret_cast<void(*)(void*,float)>(fn(SetYHeadRot));
            if (setBody) setBody(player, yaw);
            if (setHead) setHead(player, yaw);
        }
    }
    if (targetLock){
        *(float*)(rotPtr + 4) = yaw;
        if (base && !rotations){
            auto setBody = reinterpret_cast<void(*)(void*,float)>(fn(SetYBodyRot));
            auto setHead = reinterpret_cast<void(*)(void*,float)>(fn(SetYHeadRot));
            if (setBody) setBody(player, yaw);
            if (setHead) setHead(player, yaw);
        }
    }
    // Both cases handled above
}

void* hookedNormalTick(void* player) {
    void* result = oldNormalTick ? oldNormalTick(player) : nullptr;
    if (!player) return result;
    if (alwaysSprinting) reinterpret_cast<void(*)(void*,bool)>(fn(SetSprinting))(player,true);
    if (step) reinterpret_cast<void(*)(void*,float)>(fn(InitMaxAutoStep))(player,1.0625f);
    if (creativeFly && getAbilities && getLayer && setAbility){
        void* abilities = getAbilities(player);
        void* layer = abilities ? getLayer(abilities, AbilityLayerBase) : nullptr;
        if (layer){ setAbility(layer, AbilityMayFly,true); setAbility(layer, AbilityFlying,true); }
    }
    if (forceCreativeFly && lerpMotion){
        uint64_t now = clockNowNs();
        if (!g_flyXInitialized || now < g_lastTickNs || now - g_lastTickNs > 500000000ull){ spiral_flyx_reset(&g_flyXCore); g_flyXInitialized=true; }
        g_lastTickNs = now;
        FlyXInput input{}; input.now_ns = now;
        if (buildFlyXInput(player,&input)){
            FlyXVec3 motion{}; if (spiral_flyx_plan(&g_flyXCore,&input,g_upHFactor,g_upVFactor,&motion)) lerpMotion(player,&motion);
        }
    }
    // ── KillAura tick gate (Apollon TickFunctions when !useGeyserCooldown)
    if (killAura && auraType < 2){
        if (maxKillTick > g_currentKillTick && !g_canKillAuraDamage){ g_currentKillTick++; } else { g_canKillAuraDamage=true; g_currentKillTick=1; }
    }

    if (killAura && getRuntimeActors && attack && isAttackable && getHealth){
        void* level = *(void**)((uintptr_t)player + GetLevelField);
        void* gameMode = *(void**)((uintptr_t)player + GetGameModeField);
        void* posPtr = *(void**)((uintptr_t)player + GetPositionField);
        if (level && gameMode && posPtr){
            float px = *(float*)((uintptr_t)posPtr+0), py = *(float*)((uintptr_t)posPtr+4), pz = *(float*)((uintptr_t)posPtr+8);
            // Build playerList similarly to Apollon, but using getRuntimeActorList filtered as players
            std::vector<void*> playerList;
            for (void* actor : getRuntimeActors(level)){
                if (!actor || actor==player) continue;
                if (!isAttackable(actor)) continue;
                if (getHealth(actor) <=0) continue;
                void* tPos = *(void**)((uintptr_t)actor + GetPositionField);
                if (!tPos) continue;
                float d = hackDistanceTo(posPtr, tPos);
                // rotationsRange is max scan, killRange is attack
                bool inRot = d <= rotationsRange;
                bool inKill = d <= killRange;
                // Apollon: if rotations||targetLock then push if d <= rotationsRange else if d <= killRange
                bool useRot = rotations || targetLock;
                if (useRot){ if (inRot) playerList.push_back(actor); }
                else { if (inKill) playerList.push_back(actor); }
            }
            if (!playerList.empty()){
                // Ensure killRange <= rotationsRange per Apollon sliders
                if (killRange > rotationsRange) rotationsRange = killRange;
                switch (auraType){
                    case 0: { // Single — first in list
                        void* tPos = *(void**)((uintptr_t)playerList[0] + GetPositionField);
                        if (rotations || targetLock){
                            float pitch,yaw; calcPlayerAngle(px,py,pz, *(float*)((uintptr_t)tPos+0), *(float*)((uintptr_t)tPos+4), *(float*)((uintptr_t)tPos+8), pitch,yaw);
                            g_auraAngleX=pitch; g_auraAngleY=yaw; applyAuraAngle(player);
                        }
                        if (hackDistanceTo(posPtr,tPos) <= killRange){
                            bool canHurt = true; if (hurtCheck) canHurt = canBeHurted(playerList[0]);
                            if (g_canKillAuraDamage && canHurt){
                                attack(gameMode, playerList[0], true);
                                if (swing && swingFn) swingFn(player);
                                g_canKillAuraDamage=false;
                            }
                        }
                        break;
                    }
                    case 1: { // Nearest
                        void* nearest = nullptr; void* nearestKill = nullptr; float best=1e9f;
                        for (void* tgt: playerList){
                            void* tPos = *(void**)((uintptr_t)tgt + GetPositionField);
                            float d = hackDistanceTo(posPtr, tPos);
                            if (d < best && d <= rotationsRange){ best=d; nearest=tgt; if (d <= killRange) nearestKill=tgt; }
                        }
                        if (nearest){
                            void* tPos = *(void**)((uintptr_t)nearest + GetPositionField);
                            if (rotations || targetLock){
                                float pitch,yaw; calcPlayerAngle(px,py,pz, *(float*)((uintptr_t)tPos+0), *(float*)((uintptr_t)tPos+4), *(float*)((uintptr_t)tPos+8), pitch,yaw);
                                g_auraAngleX=pitch; g_auraAngleY=yaw; applyAuraAngle(player);
                            }
                            if (nearestKill){
                                bool canHurt=true; if(hurtCheck) canHurt=canBeHurted(nearest);
                                if (g_canKillAuraDamage && canHurt){
                                    attack(gameMode, nearest, true);
                                    if (swing && swingFn) swingFn(player);
                                    g_canKillAuraDamage=false;
                                }
                            }
                        }
                        break;
                    }
                    case 2: { // Multi
                        void* nearest=nullptr; void* nearestKill=nullptr; float best=1e9f;
                        for (void* tgt: playerList){
                            void* tPos = *(void**)((uintptr_t)tgt + GetPositionField);
                            float d = hackDistanceTo(posPtr, tPos);
                            if (d < best && d <= rotationsRange){ best=d; nearest=tgt; if (d <= killRange) nearestKill=tgt; }
                        }
                        if (nearest){
                            void* tPos = *(void**)((uintptr_t)nearest + GetPositionField);
                            if (rotations || targetLock){
                                float pitch,yaw; calcPlayerAngle(px,py,pz, *(float*)((uintptr_t)tPos+0), *(float*)((uintptr_t)tPos+4), *(float*)((uintptr_t)tPos+8), pitch,yaw);
                                g_auraAngleX=pitch; g_auraAngleY=yaw; applyAuraAngle(player);
                            }
                            if (nearestKill){
                                for (void* tgt: playerList){
                                    void* tPos2 = *(void**)((uintptr_t)tgt + GetPositionField);
                                    if (hackDistanceTo(posPtr,tPos2) > killRange) continue;
                                    if (maxKillTick > g_currentKillTick){ g_currentKillTick++; }
                                    else {
                                        bool canHurt=true; if(hurtCheck) canHurt=canBeHurted(tgt);
                                        if (canHurt){ attack(gameMode, tgt, true); if(swing && swingFn) swingFn(player); g_currentKillTick=1; }
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    return result;
}
void hookedSpeed(void* a1,void* a2,void* a3,void* a4,void* a5,void* a6,void* a7,void* a8,void* a9,void* a10,void* a11,void* a12,void* a13){
    if (!oldSpeed) return;
    oldSpeed(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13);
}
} // anonymous
bool initialize() {
    if (base != 0) return true;
    memory::ModuleInfo info{}; if (!memory::NativeMemory::instance().locateModule("libminecraftpe.so", info)) return false; base=info.base;
    if (GetAbilities) getAbilities = reinterpret_cast<GetAbilitiesFn>(fn(GetAbilities));
    if (GetLayer) getLayer = reinterpret_cast<GetLayerFn>(fn(GetLayer));
    if (SetAbility) setAbility = reinterpret_cast<SetAbilityFn>(fn(SetAbility));
    getRuntimeActors = reinterpret_cast<GetRuntimeActorFn>(fn(GetRuntimeActor));
    attack = reinterpret_cast<AttackFn>(fn(Attack));
    isAttackable = reinterpret_cast<IsAttackableFn>(fn(IsAttackable));
    getHealth = reinterpret_cast<GetHealthFn>(fn(GetHealth));
    lerpMotion = reinterpret_cast<LerpMotionFn>(fn(LerpMotion));
    swingFn = reinterpret_cast<SwingFn>(fn(Swing));
    if (DobbyHook(fn(NormalTick), reinterpret_cast<void*>(hookedNormalTick), reinterpret_cast<void**>(&oldNormalTick)) != RS_SUCCESS){ __android_log_print(ANDROID_LOG_ERROR,TAG,"NormalTick hook failed"); return false; }
    if (DobbyHook(fn(SpeedHack), reinterpret_cast<void*>(hookedSpeed), reinterpret_cast<void**>(&oldSpeed)) != RS_SUCCESS){ __android_log_print(ANDROID_LOG_ERROR,TAG,"Speed hook failed"); return false; }
    __android_log_print(ANDROID_LOG_INFO,TAG,"1.21.111 gameplay hooks installed: Speed/Fly/FlyX/KillAura(Apollon full)+TargetLock/Step/Sprint");
    return true;
}
void shutdown() {}
// Getters/Setters for GUI
float getFlyXUpH(){ return g_upHFactor; } void setFlyXUpH(float v){ if(v>=0.1f&&v<=2.0f) g_upHFactor=v; }
float getFlyXUpV(){ return g_upVFactor; } void setFlyXUpV(float v){ if(v>=0.1f&&v<=2.0f) g_upVFactor=v; }
float getKillRange(){ return killRange; } void setKillRange(float v){ killRange=v; if(killRange>rotationsRange) rotationsRange=killRange; }
float getRotationsRange(){ return rotationsRange; } void setRotationsRange(float v){ rotationsRange=v; if(killRange>rotationsRange) killRange=rotationsRange; }
int getAuraType(){ return auraType; } void setAuraType(int v){ if(v>=0&&v<=2) auraType=v; }
bool getSwing(){ return swing; } void setSwing(bool v){ swing=v; }
bool getRotations(){ return rotations; } void setRotations(bool v){ rotations=v; }
bool getHurtCheck(){ return hurtCheck; } void setHurtCheck(bool v){ hurtCheck=v; }
int getMaxKillTick(){ return maxKillTick; } void setMaxKillTick(int v){ if(v>=1&&v<=150) maxKillTick=v; }
} // namespace eclient_runtime::modules::gameplay
