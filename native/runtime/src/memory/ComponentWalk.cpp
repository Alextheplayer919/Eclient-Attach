#include "memory/ComponentWalk.h"
#include "memory/NativeMemory.h"
#include "profile/Offsets_1_21_111.h"

#include <cstdio>
#include <cstring>

// Disassembly evidence (1.21.111, GetHealth implementation @ 0x0CDF16D8):
//
//   ldr  x9,  [x0, #8]            ; registry = ctx->registry   (actor+0x10 absolute)
//   mov  w10, #0x613 / movk #0xfd3b lsl16   ; w10 = component type hash
//   ldp  x11, x8, [x9, #0x38]     ; bucket array [begin, end)
//   sub/lsr/sub w8, w8, #1
//   and  x8, x8, x10              ; bucketIdx = (bucketCount-1) & hash
//   ldr  x11, [x11, x8, lsl #3]   ; desc index (i64, -1 = empty)
//   ldr  w8,  [x0, #0x10]         ; entity id (packed)
//   ldr  x12, [x9, #0x50]         ; descriptor array base (0x20-byte entries)
// loop:
//   add  x13, x12, x11, lsl #5    ; desc = base + idx*0x20
//   ldr  w14, [x13, #8]           ; desc->typeHash
//   cmp w14, w10 / b.eq found
//   ldr  x11, [x12, x11, lsl #5]  ; next = desc->next (chain walk)
//   cmn x11, #1 / b.ne loop
// found:
//   ldr  x9,  [x9, #0x58]         ; descriptor array end (bounds check)
//   ldr  x9,  [x9', #0x10]        ; desc->pool object
// Pool → element (0x0CDF1748..0x0CDF17B0):
//   ldp  x10, x11, [x9, #8]       ; pages [begin, end)
//   ubfx x11, x8(eid), #0xb, #7   ; page   = (eid >> 11) & 0x7F
//   bounds: page < (end-begin)/8
//   ldr  x10, [x10, x11, lsl #3]  ; pagePtr
//   and  x11, x8, #0x3ffff ; and x11, x11, #0x7ff  ; slot = eid & 0x7FF
//   ldr  w10, [x10, x11, lsl #2]  ; packed = page[slot]
//   eor  w8, w10, (eid & 0xfffc0000); cmp w8, #0x3fffe; b.hi fail
//   ubfx x8, x10, #4, #0xe        ; elemIdx = (packed >> 4) & 0x3FFF
//   ldr  x9, [x9, #0x50]          ; pool->instances
//   and  x8, x8, #0x3ff8          ; byte offset (no lsl!)
//   ldr  x8, [x9, x8]             ; block = instances-byte-offset
//   and  x9, x10, #0x3ffff ; and x9, x9, #0x7f    ; inBlock = packed & 0x7F
//   mov  w10, #0x50               ; stride (0x50 for the Health pool)
//   madd x0, x9, x10, x8          ; element = block + inBlock*stride

namespace eclient_runtime::ecs {
namespace {
using namespace eclient_runtime::profile::mc_1_21_111;

bool rd(std::uintptr_t addr, void* out, std::size_t size) {
    return memory::NativeMemory::instance().read(addr, out, size);
}
} // namespace

std::vector<PoolDesc> listPools(std::uintptr_t actor) {
    std::vector<PoolDesc> out;
    std::uintptr_t registry = 0;
    if (!rd(actor + EcsRegistryField, &registry, sizeof(registry)) || !registry) return out;

    std::uintptr_t descBegin = 0, descEnd = 0;
    if (!rd(registry + 0x50, &descBegin, sizeof(descBegin))) return out;
    if (!rd(registry + 0x58, &descEnd, sizeof(descEnd))) return out;
    if (descEnd <= descBegin || (descEnd - descBegin) % 0x20 != 0) return out;

    const std::size_t count = (descEnd - descBegin) / 0x20;
    if (count > 8192) return out; // sanity: reject bogus registry

    out.reserve(count < 2048 ? count : 2048);
    for (std::size_t i = 0; i < count && out.size() < 2048; ++i) {
        const std::uintptr_t descAddr = descBegin + i * 0x20;
        PoolDesc d{};
        d.descAddr = descAddr;
        if (!rd(descAddr + 0x08, &d.typeHash, sizeof(d.typeHash))) continue;
        if (!rd(descAddr + 0x10, &d.pool, sizeof(d.pool))) continue;
        out.push_back(d);
    }
    return out;
}

ElementInfo resolveElement(std::uintptr_t pool, std::uint32_t eid) {
    ElementInfo info{};
    if (!pool) return info;

    std::uintptr_t pagesBegin = 0, pagesEnd = 0;
    if (!rd(pool + 0x08, &pagesBegin, sizeof(pagesBegin))) return info;
    if (!rd(pool + 0x10, &pagesEnd, sizeof(pagesEnd))) return info;
    if (pagesEnd <= pagesBegin) return info;

    const std::uint64_t pageCount = (pagesEnd - pagesBegin) / 8;
    const std::uint64_t page = (eid >> 11) & 0x7F;
    if (page >= pageCount) return info;

    std::uintptr_t pagePtr = 0;
    if (!rd(pagesBegin + page * 8, &pagePtr, sizeof(pagePtr)) || !pagePtr) return info;

    const std::uint64_t slot = eid & 0x7FF;
    std::uint32_t packed = 0;
    if (!rd(pagePtr + slot * 4, &packed, sizeof(packed))) return info;
    info.packed = packed;

    // Version/validity check, exactly as emitted: (packed ^ version) <= 0x3FFFE.
    if (((packed ^ eid) & 0xFFFC0000u) > 0x3FFFEu) return info;

    info.elemIdx = (packed >> 4) & 0x3FFF;

    std::uintptr_t instances = 0;
    if (!rd(pool + 0x50, &instances, sizeof(instances)) || !instances) return info;

    const std::uintptr_t block = [&] {
        std::uintptr_t v = 0;
        // The game masks elemIdx with 0x3FF8 and uses it as a RAW BYTE offset
        // into the instances pointer array (its paged storage granularity).
        rd(instances + (info.elemIdx & 0x3FF8), &v, sizeof(v));
        return v;
    }();
    if (!block) return info;

    info.block = block;
    info.inBlock = (packed & 0x3FFFFu) & 0x7Fu;
    info.valid = true;
    return info;
}

bool readElementFloats(std::uintptr_t address, float* out, int count) {
    if (!out || count <= 0 || count > 64) return false;
    return rd(address, out, static_cast<std::size_t>(count) * sizeof(float));
}

bool readElementWords(std::uintptr_t address, std::uint32_t* out, int count) {
    if (!out || count <= 0 || count > 64) return false;
    return rd(address, out, static_cast<std::size_t>(count) * sizeof(std::uint32_t));
}

std::string describe(const ElementInfo& info) {
    if (!info.valid) return "<invalid>";
    char buf[160];
    std::snprintf(buf, sizeof(buf), "blk=0x%llx inBlock=%llu elem=%llu packed=0x%x",
                  static_cast<unsigned long long>(info.block),
                  static_cast<unsigned long long>(info.inBlock),
                  static_cast<unsigned long long>(info.elemIdx),
                  info.packed);
    return buf;
}

} // namespace eclient_runtime::ecs
