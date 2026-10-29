// SPDX-License-Identifier: GPL-3.0-only
// Native translation of D4Cplus.zip's BypassFlyModule.kt (FlyX), retaining
// its original speed constants, phase/release logic and vertical smoothing.
// Horizontal input is analog, yaw-relative and diagonal-normalized; there is
// NO anti-lagback envelope, climb cap/pulse or server-correction handling.
#include "flyx_core.h"

#define INITIAL_H 2.42f
#define INITIAL_V 1.62f
#define INITIAL_BYPASS_H 1.1585f
#define INITIAL_BYPASS_V 1.086f
#define FINAL_H 2.18f
#define FINAL_V 1.42f
#define FINAL_BYPASS_H 1.09f
#define FINAL_BYPASS_V 1.05f
#define GLIDE -0.1266f
#define RELEASE_DELAY 0.09f
#define DESCENT_SPEED 4.2997f
#define DESCENT_H 1.1f

static float absolute(float value) { return __builtin_fabsf(value); }
static float clamp_axis(float value) {
    return value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
}
static FlyXBool finite(float value) { return __builtin_isfinite(value); }

void spiral_flyx_reset(FlyXCore *s) {
    s->phase_start_ns = 0;
    s->vx = 0.0f;
    s->vy = GLIDE;
    s->vz = 0.0f;
    s->bypass_phase = 0;
    s->alternate_tick = 0;
}

FlyXBool spiral_flyx_plan(FlyXCore *s, const FlyXInput *in,
                         float up_h_factor, float up_v_factor, FlyXVec3 *out) {
    if (!finite(in->strafe) || !finite(in->forward) || !finite(in->yaw_degrees) ||
        !finite(in->position_x) || !finite(in->position_z) ||
        !finite(up_h_factor) || !finite(up_v_factor) ||
        up_h_factor < 0.1f || up_h_factor > 2.0f || up_v_factor < 0.1f || up_v_factor > 2.0f)
        return 0;
    const float strafe = clamp_axis(in->strafe), forward = clamp_axis(in->forward);
    const float magnitude = spiral_flyx_sqrt(strafe * strafe + forward * forward);
    const FlyXBool moving = magnitude > 0.04f;
    float t = spiral_flyx_sqrt(in->position_x * in->position_x + in->position_z * in->position_z) / 5500000.0f;
    if (t > 1.0f) t = 1.0f;
    const float norm_h = INITIAL_H + (FINAL_H - INITIAL_H) * t;
    const float norm_v = INITIAL_V + (FINAL_V - INITIAL_V) * t;
    const float bypass_h = INITIAL_BYPASS_H + (FINAL_BYPASS_H - INITIAL_BYPASS_H) * t;
    const float bypass_v = INITIAL_BYPASS_V + (FINAL_BYPASS_V - INITIAL_BYPASS_V) * t;

    FlyXBool bypass_active = 0;
    if (in->jump && moving) {
        s->bypass_phase = 1;
        s->phase_start_ns = in->now_ns;
        bypass_active = 1;
    } else if (s->bypass_phase) {
        bypass_active = 1;
        // Same release-delay phase as the source, not rubberband recovery.
        const float seconds = (float)(in->now_ns - s->phase_start_ns) / 1.0e9f;
        if (in->now_ns < s->phase_start_ns || seconds >= RELEASE_DELAY) {
            s->bypass_phase = 0;
            bypass_active = 0;
        }
    }

    // Neutral 1.0x retains the original vertical target exactly. The requested
    // Up V Factor scales upward lift only, not glide/descent or base constants.
    const float target_vy = in->sneak ? -DESCENT_SPEED :
        GLIDE + (in->jump ? (bypass_active ? bypass_v : norm_v) * up_v_factor : 0.0f);
    s->vy += (target_vy - s->vy) * (target_vy > s->vy ? 0.22f : 0.35f);
    if (absolute(target_vy - s->vy) < 0.01f) s->vy = target_vy;

    if (!moving) {
        s->vx += (0.0f - s->vx) * 0.35f;
        s->vz += (0.0f - s->vz) * 0.35f;
        if (absolute(s->vx) < 0.01f && absolute(s->vz) < 0.01f) s->vx = s->vz = 0.0f;
    } else {
        float speed = in->sneak ? DESCENT_H : in->touching_terrain ? 2.0f :
            bypass_active ? bypass_h : norm_h;
        if (in->jump && !in->sneak) speed *= up_h_factor;
        const float scale = speed / (magnitude > 1.0f ? magnitude : 1.0f);
        const float yaw = in->yaw_degrees * 0.017453292519943295f;
        const float sin_yaw = spiral_flyx_sin(yaw), cos_yaw = spiral_flyx_cos(yaw);
        const float vx = (strafe * cos_yaw - forward * sin_yaw) * scale;
        const float vz = (forward * cos_yaw + strafe * sin_yaw) * scale;
        s->vx += (vx - s->vx) * 0.32f;
        s->vz += (vz - s->vz) * 0.32f;
        // Preserve the source's alternating moving descent, not a new envelope.
        if (in->sneak) {
            if ((s->alternate_tick++ & 1) == 0) {
                *out = (FlyXVec3){s->vx, GLIDE, s->vz};
            } else {
                *out = (FlyXVec3){0.0f, -DESCENT_SPEED, 0.0f};
            }
            return 1;
        }
    }
    *out = (FlyXVec3){s->vx, s->vy, s->vz};
    return finite(out->x) && finite(out->y) && finite(out->z);
}
