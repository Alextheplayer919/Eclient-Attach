// SPDX-License-Identifier: GPL-3.0-only
// FlyX only, layered over byte-identical AutoTotem test2 and RGB. No new game
// hook, packet interception, anti-lagback, ability grants or auth modification.
#include "flyx_bindings.h"
#define HIDDEN __attribute__((visibility("hidden")))
typedef unsigned int u32;
typedef unsigned char boolean;
typedef FlyXU64 (*OriginalTick)(void *);

typedef struct {
    u32 control, up_h_bits, up_v_bits, last_control;
    FlyXPtr player, world;
    FlyXU64 last_tick_ns;
    FlyXCore core;
    boolean busy;
} FlyXState;
_Static_assert(sizeof(FlyXState) == 72, "private FlyX state extent");
_Static_assert(__builtin_offsetof(FlyXState, core) == 40, "FlyX core offset");

extern HIDDEN FlyXState spiral_flyx_state;
extern HIDDEN volatile FlyXPtr spiral_game_base;
extern HIDDEN volatile unsigned char spiral_gameplay_suspended;
extern HIDDEN volatile u32 spiral_selected_category;
extern HIDDEN FlyXU64 spiral_clock_now(void);
extern HIDDEN FlyXU64 spiral_autototem_tick(void *, OriginalTick);
extern HIDDEN void spiral_autototem_gui(void);
extern HIDDEN boolean spiral_imgui_checkbox(const char *, boolean *);
extern HIDDEN boolean spiral_imgui_slider_float(const char *, float *, float, float, const char *, int);

static FlyXPtr ptr(const void *object, unsigned int offset) {
    return *(const FlyXPtr *)((FlyXPtr)object + offset);
}
static float factor(u32 bits) {
    union { u32 bits; float value; } v = {bits};
    return bits ? v.value : 1.0f; // RW zero-fill default = neutral factor.
}
static u32 bits(float value) { union { float value; u32 bits; } v = {value}; return v.bits; }

HIDDEN FlyXU64 spiral_flyx_tick(void *player, OriginalTick original) {
    FlyXState *s = &spiral_flyx_state;
    const u32 control = __atomic_load_n(&s->control, __ATOMIC_ACQUIRE);
    if (!(control & 1) || s->busy) {
        // Never read a game object, change its velocity or reset native module
        // state when FlyX is disabled. The exact installed AutoTotem wrapper
        // still forwards the captured original tick and return value once.
        return spiral_autototem_tick(player, original);
    }
    s->busy = 1;
    const FlyXU64 result = spiral_autototem_tick(player, original);
    const FlyXPtr game = spiral_game_base;
    FlyXInput input;
    if (control != __atomic_load_n(&s->control, __ATOMIC_ACQUIRE) || spiral_gameplay_suspended ||
        !game || (game & 4095) || !player || ptr(player, 0) != game + 0x10036a70 ||
        !ptr(player, 0x1d0) ||
        *(const FlyXPtr *)(game + 0x10036a70 + 23 * 8) != game + 0xc94c3f0 ||
        !spiral_flyx_snapshot(game, player, &input)) {
        s->last_control = 0;
        s->busy = 0;
        return result;
    }
    input.now_ns = spiral_clock_now();
    if (s->last_control != control || s->player != (FlyXPtr)player ||
        s->world != ptr(player, 0x1d0) || input.now_ns < s->last_tick_ns ||
        input.now_ns - s->last_tick_ns > 500000000u) {
        // Lifecycle/pause reset only. No server-position/correction tracking.
        spiral_flyx_reset(&s->core);
    }
    s->last_control = control;
    s->player = (FlyXPtr)player;
    s->world = ptr(player, 0x1d0);
    s->last_tick_ns = input.now_ns;
    FlyXVec3 motion;
    if (spiral_flyx_plan(&s->core, &input,
            factor(__atomic_load_n(&s->up_h_bits, __ATOMIC_ACQUIRE)),
            factor(__atomic_load_n(&s->up_v_bits, __ATOMIC_ACQUIRE)), &motion) &&
        control == __atomic_load_n(&s->control, __ATOMIC_ACQUIRE) && !spiral_gameplay_suspended) {
        // Actual LocalPlayer slot 23, Actor::lerpMotion(Vec3 const&). The native
        // body writes only StateVectorComponent's three velocity floats.
        ((void (*)(void *, const FlyXVec3 *))(game + 0xc94c3f0))(player, &motion);
    }
    s->busy = 0;
    return result;
}

HIDDEN void spiral_flyx_gui(void) {
    // Original BeginGroup and fixed Combat AutoTotem row remain in the exact
    // old helper. It is called once, including in every non-Movement category.
    spiral_autototem_gui();
    if (spiral_selected_category != 0) return;
    FlyXState *s = &spiral_flyx_state;
    const u32 control = __atomic_load_n(&s->control, __ATOMIC_ACQUIRE);
    boolean selected = (control & 1) != 0;
    if (spiral_imgui_checkbox("FlyX", &selected)) {
        // Bump generation even for rapid OFF/ON between game ticks.
        __atomic_store_n(&s->control, ((control & ~1u) + 2) | selected, __ATOMIC_RELEASE);
    }
    float h = factor(__atomic_load_n(&s->up_h_bits, __ATOMIC_ACQUIRE));
    float v = factor(__atomic_load_n(&s->up_v_bits, __ATOMIC_ACQUIRE));
    if (spiral_imgui_slider_float("Up H Factor##FlyX", &h, 0.1f, 2.0f, "%.2fx", 0) &&
        __builtin_isfinite(h) && h >= 0.1f && h <= 2.0f)
        __atomic_store_n(&s->up_h_bits, bits(h), __ATOMIC_RELEASE);
    if (spiral_imgui_slider_float("Up V Factor##FlyX", &v, 0.1f, 2.0f, "%.2fx", 0) &&
        __builtin_isfinite(v) && v >= 0.1f && v <= 2.0f)
        __atomic_store_n(&s->up_v_bits, bits(v), __ATOMIC_RELEASE);
}
