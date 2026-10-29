// SPDX-License-Identifier: GPL-3.0-only
#ifndef SPIRAL_FLYX_CORE_H
#define SPIRAL_FLYX_CORE_H

typedef __UINT64_TYPE__ FlyXU64;
typedef unsigned char FlyXBool;
typedef struct { float x, y, z; } FlyXVec3;
typedef struct {
    float strafe, forward, yaw_degrees, position_x, position_z;
    FlyXBool jump, sneak, touching_terrain;
    FlyXU64 now_ns;
} FlyXInput;
typedef struct {
    FlyXU64 phase_start_ns;
    float vx, vy, vz;
    FlyXBool bypass_phase, alternate_tick;
} FlyXCore;

// Implemented by the existing native engine's libm PLTs, or libm in host tests.
float spiral_flyx_sin(float angle);
float spiral_flyx_cos(float angle);
float spiral_flyx_sqrt(float value);
void spiral_flyx_reset(FlyXCore *state);
FlyXBool spiral_flyx_plan(FlyXCore *state, const FlyXInput *input,
                        float up_h_factor, float up_v_factor, FlyXVec3 *result);
#endif
