// SPDX-License-Identifier: GPL-3.0-only
#ifndef SPIRAL_FLYX_BINDINGS_H
#define SPIRAL_FLYX_BINDINGS_H
#include "flyx_core.h"
typedef __UINTPTR_TYPE__ FlyXPtr;
// Read-only exact-Android EnTT lookup; missing components return NULL, never
// call the original asserting getter or allocate/add components.
FlyXPtr spiral_flyx_component(const void *player, unsigned int type_hash, unsigned int stride);
FlyXBool spiral_flyx_snapshot(FlyXPtr game, void *player, FlyXInput *input);
#endif
