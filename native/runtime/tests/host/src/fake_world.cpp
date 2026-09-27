// fake_world.cpp — see fake_world.h for why this exists.
#include "fake_world.h"

#include "memory/NativeMemory.h"
#include "profile/Offsets_1_21_111.h"

#include <sys/mman.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fakeworld {
namespace {

using namespace eclient_runtime::profile::mc_1_21_111;

constexpr std::size_t kArenaSize = 256u * 1024 * 1024;   // covers every offset
constexpr int kRegistryDescs = 620;                       // realistic registry size
constexpr std::uint32_t kPlayerTypeId = 319;              // a real player type id
constexpr std::uint32_t kZombieTypeId = 999;

void* g_arena = nullptr;
void* g_player = nullptr;
std::vector<void*> g_actors;
std::vector<void*> g_actorPointers;    // what getRuntimeActorList hands back
Stats g_stats;

// ── the game's functions, as far as the modules are concerned ──────────────

// The module calls this through `std::vector<void*> (*)(void*)`, i.e. the
// return value travels in the sret register. Returning anything else silently
// corrupts the caller — this must stay a vector by value.
std::vector<void*> getRuntimeActorListStub(void* /*level*/) {
    g_stats.runtimeActorCalls++;
    g_stats.actorPointersReturned += static_cast<long>(g_actorPointers.size());
    return g_actorPointers;
}

void attackStub(void* /*gameMode*/, void* /*target*/, bool /*swing*/) {
    g_stats.attacks++;
}

bool isAttackableStub(void* /*actor*/) { return true; }

int getHealthStub(void* actor) {
    return *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(actor) + 0x20);
}

std::uint32_t getEntityTypeIdStub(void* actor) {
    return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(actor) + 0x28);
}

void* getAbilitiesStub(void* /*player*/) { return &g_stats; }        // any non-null
void* getLayerStub(void* /*abilities*/, int /*index*/) { return &g_stats; }
void setAbilityStub(void* /*layer*/, int /*index*/, bool /*on*/) { g_stats.abilityCalls++; }
void setClimbingStub(void* /*actor*/, bool /*on*/) {}
void setYHeadRotStub(void* /*actor*/, float /*yaw*/) {}
float getYHeadRotStub(void* /*actor*/) { return 0.0f; }
void setYBodyRotStub(void* /*mob*/, float /*yaw*/) {}

// ── trampolines: base + offset must be callable ────────────────────────────
// movabs rax, <target>; jmp rax
void writeTrampoline(std::uintptr_t where, void* target) {
    auto* p = reinterpret_cast<std::uint8_t*>(where);
    p[0] = 0x48; p[1] = 0xB8;
    const auto t = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(p + 2, &t, sizeof(t));
    p[10] = 0xFF; p[11] = 0xE0;
}

// ── world construction ─────────────────────────────────────────────────────

// The ECS registry: actor + EcsRegistryField -> registry
//   +0x50 descriptor array begin, +0x58 end, entries are 0x20 bytes with the
//   type hash at +0x08 and the pool object at +0x10.
struct Registry {
    std::vector<std::uint8_t> descs;
    std::vector<std::uint8_t> typePool;
    std::vector<std::uint8_t> pages;      // page pointer array
    std::vector<std::uint8_t> instances;  // block pointer array
    std::vector<std::vector<std::uint8_t>> blocks;
};

Registry* buildRegistry(int actorCount) {
    auto* reg = new Registry();

    reg->descs.assign(static_cast<std::size_t>(kRegistryDescs) * 0x20, 0);

    // The ActorType pool, laid out the way resolveElement() reads it:
    //   pool + 0x08 pages begin, +0x10 pages end, +0x50 instances
    reg->typePool.assign(0x60, 0);
    reg->pages.assign(2048 * sizeof(void*), 0);   // slot = eid & 0x7FF
    reg->instances.assign((static_cast<std::size_t>(actorCount) + 1) * 8 * sizeof(void*), 0);

    for (int i = 0; i < actorCount; ++i) {
        // elemIdx must keep its low 3 bits clear (the game indexes the block
        // pointer array in BYTES), and inBlock must stay inside the low nibble
        // so packed decodes unambiguously.
        const std::uint32_t elemIdx = 8u * static_cast<std::uint32_t>(i);
        const std::uint32_t inBlock = static_cast<std::uint32_t>(i % 16);
        const std::uint32_t packed = (elemIdx << 4) | inBlock;

        // entity id: must agree with `packed` in bits 18..31 for the validity
        // check, so stay below 0x40000 and keep page 0 / slot = eid.
        const std::uint32_t eid = 1000u + static_cast<std::uint32_t>(i);
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(g_actors[static_cast<std::size_t>(i)]) + EntityIdField) = eid;

        auto* page = reinterpret_cast<std::uint32_t*>(reg->pages.data());
        page[eid & 0x7FF] = packed;

        // one block per actor, element at inBlock * stride(4)
        reg->blocks.emplace_back(64, 0);
        auto& block = reg->blocks.back();
        const std::uint32_t typeId = (i % 4 == 3) ? kZombieTypeId : kPlayerTypeId;
        *reinterpret_cast<std::uint32_t*>(block.data() + inBlock * ActorTypeComponentStride) = typeId;
        *reinterpret_cast<void**>(reg->instances.data() + (elemIdx & 0x3FF8)) = block.data();
    }

    // fill the pool header
    void** pagesBegin = reinterpret_cast<void**>(reg->typePool.data() + 0x08);
    void** pagesEnd = reinterpret_cast<void**>(reg->typePool.data() + 0x10);
    *pagesBegin = reg->pages.data();
    *pagesEnd = reg->pages.data() + reg->pages.size();
    *reinterpret_cast<void**>(reg->typePool.data() + 0x50) = reg->instances.data();

    // descriptor 0 is the type pool; the rest are decoys so findPoolByHash has
    // a realistic registry to walk through (this is what the r38 cache fixed).
    for (int i = 0; i < kRegistryDescs; ++i) {
        std::uint8_t* d = reg->descs.data() + static_cast<std::size_t>(i) * 0x20;
        const std::uint32_t hash = (i == 0) ? ActorTypeComponentHash : (0x1000u + static_cast<std::uint32_t>(i));
        *reinterpret_cast<std::uint32_t*>(d + 0x08) = hash;
        *reinterpret_cast<void**>(d + 0x10) = (i == 0) ? reg->typePool.data() : nullptr;
    }
    return reg;
}

Registry* g_registry = nullptr;

void* allocBlock(std::size_t size) {
    auto* p = static_cast<std::uint8_t*>(std::calloc(1, size));
    return p;
}

} // namespace

void setup(int actorCount) {
    teardown();

    // A fresh arena is a genuine "the map changed" event, and the runtime
    // exposes exactly that hint. The bench does NOT rely on it to paper over a
    // broken cache: bench_memory.cpp separately proves that a freshly mapped
    // region is readable on the very first read without any hint.
    eclient_runtime::memory::NativeMemory::instance().invalidateMapCache();

    // ── the executable arena ───────────────────────────────────────────────
    g_arena = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_arena == MAP_FAILED) {
        std::printf("fakeworld: arena mmap failed\n");
        g_arena = nullptr;
        return;
    }
    const auto baseAddr = reinterpret_cast<std::uintptr_t>(g_arena);
    writeTrampoline(baseAddr + GetRuntimeActor, reinterpret_cast<void*>(getRuntimeActorListStub));
    writeTrampoline(baseAddr + Attack, reinterpret_cast<void*>(attackStub));
    writeTrampoline(baseAddr + IsAttackable, reinterpret_cast<void*>(isAttackableStub));
    writeTrampoline(baseAddr + GetHealth, reinterpret_cast<void*>(getHealthStub));
    writeTrampoline(baseAddr + GetEntityTypeId, reinterpret_cast<void*>(getEntityTypeIdStub));
    writeTrampoline(baseAddr + GetAbilities, reinterpret_cast<void*>(getAbilitiesStub));
    writeTrampoline(baseAddr + GetLayer, reinterpret_cast<void*>(getLayerStub));
    writeTrampoline(baseAddr + SetAbility, reinterpret_cast<void*>(setAbilityStub));
    writeTrampoline(baseAddr + SetClimbing, reinterpret_cast<void*>(setClimbingStub));
    writeTrampoline(baseAddr + SetYHeadRot, reinterpret_cast<void*>(setYHeadRotStub));
    writeTrampoline(baseAddr + GetYHeadRot, reinterpret_cast<void*>(getYHeadRotStub));
    writeTrampoline(baseAddr + SetYBodyRot, reinterpret_cast<void*>(setYBodyRotStub));

    // ── the actors (allocated before the registry: it needs their ids) ─────
    g_actors.clear();
    g_actorPointers.clear();
    for (int i = 0; i < actorCount; ++i) {
        void* actor = allocBlock(0xC00);          // bigger than every field used
        g_actors.push_back(actor);
        g_actorPointers.push_back(actor);

        // state vector: pos @+0, prevPos @+0x0C, velocity @+0x18
        auto* sv = static_cast<float*>(allocBlock(0x40));
        sv[0] = 10.0f + static_cast<float>(i);    // x
        sv[1] = 64.0f;                            // y
        sv[2] = -3.0f - static_cast<float>(i);    // z
        sv[3] = sv[0]; sv[4] = sv[1]; sv[5] = sv[2];
        *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField) = sv;

        // the two things the stub accessors read
        *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(actor) + 0x20) = 20 - (i % 5);
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(actor) + 0x28) = kPlayerTypeId;

        // rotation Vec2 pointed at by 0x218
        auto* rot = static_cast<float*>(allocBlock(8));
        rot[0] = 0.0f; rot[1] = 0.0f;
        *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + RotationPtrField) = rot;

        *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetGameModeField) =
            allocBlock(0x40);
        // a fake level per actor keeps the modules' null gates satisfied
        *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetLevelField) =
            allocBlock(0x40);
    }

    // ── the local player ───────────────────────────────────────────────────
    g_player = allocBlock(0xC00);
    auto* sv = static_cast<float*>(allocBlock(0x40));
    sv[0] = 10.0f; sv[1] = 64.0f; sv[2] = -3.0f;
    sv[3] = sv[0]; sv[4] = sv[1]; sv[5] = sv[2];
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(g_player) + GetPositionField) = sv;
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(g_player) + GetLevelField) = allocBlock(0x40);
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(g_player) + GetGameModeField) = allocBlock(0x40);
    auto* rot = static_cast<float*>(allocBlock(8));
    rot[0] = 0.0f; rot[1] = 0.0f;
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(g_player) + RotationPtrField) = rot;

    // ── the registry (needs the actors' entity ids) ────────────────────────
    g_registry = buildRegistry(actorCount);
    void* registry = g_registry;
    if (actorCount > 0) {
        // every actor shares the world's registry, exactly like the game
        for (void* actor : g_actors) {
            *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + EcsRegistryField) = registry;
        }
    }
    // the player needs the registry too (its own type id is read from it)
    // and the registry's descriptor array bounds
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(g_player) + EcsRegistryField) = registry;
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(registry) + 0x50) = g_registry->descs.data();
    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(registry) + 0x58) =
        g_registry->descs.data() + g_registry->descs.size();

    // the player itself needs a type id + entity id, like any other actor
    const std::uint32_t playerEid = 1000u + static_cast<std::uint32_t>(actorCount);
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(g_player) + EntityIdField) = playerEid;

    // give the player its own entry in the type pool so "my type" resolves
    {
        auto* reg = g_registry;
        const std::uint32_t idx = static_cast<std::uint32_t>(actorCount);
        const std::uint32_t elemIdx = 8u * idx;
        const std::uint32_t inBlock = idx % 16;
        const std::uint32_t packed = (elemIdx << 4) | inBlock;
        if ((elemIdx & 0x3FF8) < reg->instances.size()) {
            auto* page = reinterpret_cast<std::uint32_t*>(reg->pages.data());
            page[playerEid & 0x7FF] = packed;
            reg->blocks.emplace_back(64, 0);
            auto& block = reg->blocks.back();
            *reinterpret_cast<std::uint32_t*>(block.data() + inBlock * ActorTypeComponentStride) =
                kPlayerTypeId;
            *reinterpret_cast<void**>(reg->instances.data() + (elemIdx & 0x3FF8)) = block.data();
        }
    }
}

void teardown() {
    if (g_arena) {
        munmap(g_arena, kArenaSize);
        g_arena = nullptr;
    }
    g_actorPointers.clear();
    g_actors.clear();
    g_registry = nullptr;   // diagnostic-only: process exit reclaims the rest
    g_stats = Stats{};
}

std::uintptr_t base() { return reinterpret_cast<std::uintptr_t>(g_arena); }
void* player() { return g_player; }
const std::vector<void*>& actorPointers() { return g_actorPointers; }
Stats& stats() { return g_stats; }

} // namespace fakeworld
