#pragma once
// Experimental 1.21.111 ARM64 candidates from the older FlyX binding. All
// pointer reads go through process_vm_readv so bad guesses fail closed.
#include "fly2_calculated.h"
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cmath>
namespace eclient::fly2::probe {
template<class T> static bool read(std::uintptr_t address, T& out) {
    if (address < 4096 || address > UINTPTR_MAX - sizeof(T)) return false;
    iovec local{&out, sizeof(T)}, remote{reinterpret_cast<void*>(address), sizeof(T)};
    return syscall(__NR_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(sizeof(T));
}
static bool aligned(std::uintptr_t p) { return p >= 4096 && !(p & 7); }
static bool span(std::uintptr_t begin, std::uintptr_t end, unsigned stride, std::uintptr_t max, std::uintptr_t& count) {
    if (!aligned(begin) || end < begin || (end - begin) % stride) return false;
    count = (end-begin)/stride; return count && count <= max;
}
static std::uintptr_t component(std::uintptr_t actor) {
    constexpr std::uint32_t hash = 0x018b1887;
    std::uintptr_t registry, buckets, bucketsEnd, nodes, nodesEnd, bucketCount, nodeCount;
    if (!read(actor+0x10,registry) || !aligned(registry) ||
        !read(registry+0x38,buckets) || !read(registry+0x40,bucketsEnd) ||
        !read(registry+0x50,nodes) || !read(registry+0x58,nodesEnd) ||
        !span(buckets,bucketsEnd,8,1u<<20,bucketCount) || (bucketCount & (bucketCount-1)) ||
        !span(nodes,nodesEnd,32,1u<<20,nodeCount)) return 0;
    std::uintptr_t idx, pool = 0;
    if (!read(buckets+(hash & (bucketCount-1))*8,idx)) return 0;
    for (unsigned n=0; n<64 && idx<nodeCount; ++n) {
        std::uintptr_t node=nodes+idx*32; std::uint32_t key;
        if (!read(node+8,key)) return 0;
        if (key==hash) { if (!read(node+0x10,pool)) return 0; break; }
        if (!read(node,idx)) return 0;
    }
    if (!aligned(pool)) return 0;
    std::uint32_t entity, packed; std::uintptr_t sparse, end, count, entries, pages, pagesEnd, pageCount, data;
    if (!read(actor+0x18,entity) || !read(pool+8,sparse) || !read(pool+0x10,end) ||
        !span(sparse,end,8,128,count) || ((entity>>11)&127)>=count ||
        !read(sparse+((entity>>11)&127)*8,entries) || !aligned(entries) ||
        !read(entries+(entity&2047)*4,packed) || (packed ^ (entity & 0xfffc0000u)) >= 0x3ffffu ||
        !read(pool+0x50,pages) || !read(pool+0x58,pagesEnd) || !span(pages,pagesEnd,8,2048,pageCount) ||
        ((packed & 0x3ffffu)>>7)>=pageCount || !read(pages+((packed & 0x3ffffu)>>7)*8,data) || !aligned(data)) return 0;
    return data+(packed&127)*160;
}
struct Raw {
    NativeSnapshot snapshot{};
    float rawX = 0, rawY = 0, analogX = 0, analogY = 0;
    unsigned jump = 0, sneak = 0;
};
static bool sample(std::uintptr_t actor, Raw& out) {
    out = {};
    const auto move = component(actor);
    std::uintptr_t position, rotation;
    std::uint8_t jump, sneak;
    if (!move || !read(actor+0x208,position) || !read(actor+0x218,rotation) ||
        !read(position,out.snapshot.input.position) || !read(rotation+4,out.snapshot.input.yawDegrees) ||
        !read(move+0x58,out.snapshot.input.strafe) || !read(move+0x5c,out.snapshot.input.forward) ||
        !read(move+0x3c,out.rawX) || !read(move+0x40,out.rawY) ||
        !read(move+0x14,out.analogX) || !read(move+0x18,out.analogY) ||
        !read(move+0x98,jump) || !read(move+0x94,sneak) || jump>1 || sneak>1) return false;
    const auto& i = out.snapshot.input;
    if (!std::isfinite(i.position.x) || !std::isfinite(i.position.y) || !std::isfinite(i.position.z) ||
        !std::isfinite(i.yawDegrees) || !std::isfinite(i.strafe) || !std::isfinite(i.forward) ||
        std::fabs(i.strafe)>1.001f || std::fabs(i.forward)>1.001f) return false;
    out.jump=jump; out.sneak=sneak;
    out.snapshot.playerIdentity=actor;
    // Experimental assertion, NOT evidence that the field semantics are correct.
    out.snapshot.layoutVerified=true;
    out.snapshot.input.up=jump; out.snapshot.input.down=sneak;
    return true;
}
}
