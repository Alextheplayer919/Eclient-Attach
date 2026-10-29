// SPDX-License-Identifier: GPL-3.0-only
// Pin against original Android native lookups/field consumers, NOT Windows
// class offsets. No packet constructor, privilege or game object is modified.
#include "flyx_bindings.h"

typedef unsigned int u32;
static FlyXPtr ptr(FlyXPtr object, unsigned int offset) { return *(const FlyXPtr *)(object + offset); }
static u32 word(FlyXPtr object, unsigned int offset) { return *(const u32 *)(object + offset); }
static unsigned char byte(FlyXPtr object, unsigned int offset) { return *(const unsigned char *)(object + offset); }
static float scalar(FlyXPtr object, unsigned int offset) { return *(const float *)(object + offset); }
static FlyXBool aligned(FlyXPtr address) { return address >= 4096 && (address & 7) == 0; }
static FlyXPtr span(FlyXPtr begin, FlyXPtr end, unsigned int stride, FlyXPtr maximum) {
    if (!aligned(begin) || !aligned(end) || end < begin || (end - begin) % stride) return 0;
    const FlyXPtr count = (end - begin) / stride;
    return count <= maximum ? count : 0;
}

FlyXPtr spiral_flyx_component(const void *player, u32 hash, unsigned int stride) {
    // Actor's actual EntityContext registry/entity members, as used by
    // 0x6bf1d18 (MoveInputComponent) and 0xcdf9fb4 (horizontal collision flag).
    const FlyXPtr actor = (FlyXPtr)player, registry = ptr(actor, 0x10);
    if (!aligned(registry)) return 0;
    const FlyXPtr buckets = ptr(registry, 0x38), nodes = ptr(registry, 0x50);
    const FlyXPtr count = span(buckets, ptr(registry, 0x40), 8, 1u << 20);
    const FlyXPtr node_count = span(nodes, ptr(registry, 0x58), 32, 1u << 20);
    if (!count || (count & (count - 1)) || !node_count) return 0;
    FlyXPtr index = ptr(buckets, (hash & (count - 1)) * 8), pool = 0;
    for (unsigned int tries = 0; tries < 64 && index < node_count; ++tries) {
        const FlyXPtr node = nodes + index * 32;
        if (word(node, 8) == hash) { pool = ptr(node, 0x10); break; }
        index = ptr(node, 0);
    }
    if (!aligned(pool)) return 0;
    const u32 entity = word(actor, 0x18);
    const FlyXPtr sparse = ptr(pool, 8);
    const FlyXPtr sparse_count = span(sparse, ptr(pool, 0x10), 8, 128);
    const unsigned int sparse_page = (entity >> 11) & 127;
    if (sparse_page >= sparse_count) return 0;
    const FlyXPtr sparse_entries = ptr(sparse, sparse_page * 8);
    if (!aligned(sparse_entries)) return 0;
    const u32 packed = word(sparse_entries, (entity & 2047) * 4);
    if ((packed ^ (entity & 0xfffc0000u)) >= 0x3ffffu) return 0;
    if (!stride) return 1; // Empty flag storage: sparse membership is sufficient.
    const FlyXPtr pages = ptr(pool, 0x50);
    const FlyXPtr page_count = span(pages, ptr(pool, 0x58), 8, 2048);
    const unsigned int page = (packed & 0x3ffffu) >> 7;
    if (page >= page_count) return 0;
    const FlyXPtr data = ptr(pages, page * 8);
    return aligned(data) ? data + (packed & 127) * stride : 0;
}

static FlyXBool axes_values(float x, float y, FlyXInput *input) {
    // Native unset analog vectors are sentinels, not a full-speed stick input.
    if (!__builtin_isfinite(x) || !__builtin_isfinite(y) || __builtin_fabsf(x) > 1.001f || __builtin_fabsf(y) > 1.001f)
        return 0;
    if (__builtin_fabsf(x) <= 0.04f && __builtin_fabsf(y) <= 0.04f) return 0;
    input->strafe = x;
    input->forward = y;
    return 1;
}

static FlyXBool axes(FlyXPtr object, unsigned int offset, FlyXInput *input) {
    return axes_values(scalar(object, offset), scalar(object, offset + 4), input);
}
static FlyXBool raw_axes(FlyXPtr game, FlyXPtr move, FlyXInput *input) {
    float x = scalar(move, 0x3c), y = scalar(move, 0x40);
    if (x == scalar(game + 0x34f189c, 0) && y == scalar(game + 0x34f189c, 4)) {
        // Match the native packet consumer's exact unset-vector fallback. The
        // pinned helper is pure/read-only; it includes raw diagonal buttons.
        typedef struct { float x, y; } NativeVec2;
        const NativeVec2 raw = ((NativeVec2 (*)(const void *))(game + 0x9d03e90))((const void *)(move + 0x28));
        x = raw.x;
        y = raw.y;
    }
    return axes_values(x, y, input);
}

FlyXBool spiral_flyx_snapshot(FlyXPtr game, void *player, FlyXInput *input) {
    const FlyXPtr actor = (FlyXPtr)player;
    const FlyXPtr move = spiral_flyx_component(player, 0x018b1887, 160);
    if (!move) return 0;
    // 0x660266c copies native mMove at +0x58 into packet motion, logical analog
    // at +0x14 into analogMoveVector, and raw analog at +0x3c into rawMoveVector.
    // Keep the source's motion -> raw -> analog -> digital fallback priority.
    if (!axes(move, 0x58, input) && !raw_axes(game, move, input) && !axes(move, 0x14, input)) {
        if (byte(move, 0xf) > 1 || byte(move, 0x10) > 1 || byte(move, 0xd) > 1 || byte(move, 0xe) > 1) return 0;
        input->strafe = (float)((int)byte(move, 0xf) - byte(move, 0x10));
        input->forward = (float)((int)byte(move, 0xd) - byte(move, 0xe));
    }
    // Native packet flags 6/8/7 = jumping/sneaking/auto-jumping-in-water.
    if (byte(move, 0x98) > 1 || byte(move, 0x94) > 1 || byte(move, 0x99) > 1) return 0;
    input->jump = byte(move, 0x98);
    input->sneak = byte(move, 0x94);
    input->touching_terrain = byte(move, 0x99) || spiral_flyx_component(player, 0xd5ee0bd8u, 0);
    const FlyXPtr position = ptr(actor, 0x208), rotation = ptr(actor, 0x218);
    // StateVector has 36-byte stride/alignment 4; odd dense elements are NOT
    // necessarily 8-byte aligned. ActorRotation has a 16-byte dense stride.
    if (position < 4096 || (position & 3) || !aligned(rotation)) return 0;
    input->position_x = scalar(position, 0);
    input->position_z = scalar(position, 8);
    input->yaw_degrees = scalar(rotation, 4);
    return 1;
}
