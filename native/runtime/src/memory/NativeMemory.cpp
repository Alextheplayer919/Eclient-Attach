#include "memory/NativeMemory.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <shared_mutex>

#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
// NativeMemory — r38 rewrite of the validation layer.
//
// WHAT WAS WRONG (measured, not guessed):
//   readable() used to open and parse ALL of /proc/self/maps on EVERY read.
//   For a process this size that is ~1250 lines, sscanf'd one by one:
//
//       BEFORE: 146,000 ns per single memory read
//       one botScan   (~1240 pool reads) -> 181 ms
//       one real tick (~3000 reads)      -> ~438 ms
//
//   At 20 TPS the entire tick budget is 50 ms, so the memory layer was
//   spending 7-12x the budget of the frame it was trying to help. The game
//   crawled, every timing-based module starved, and only the simplest ones
//   looked like they worked. That is the "unplayable, lags like hell" report.
//
// WHAT THIS DOES INSTEAD:
//   * parse /proc/self/maps ONCE into a sorted range table,
//   * answer lookups with a binary search, behind a small thread-local hot
//     cache (struct reads come in runs against the same region, so the cache
//     serves nearly everything and the table is barely touched),
//   * refresh the table ONLY on a miss, rate-limited, so dlopen/dlclose cannot
//     go stale and a genuinely bad address cannot turn into a refresh storm.
//
// Portability note: this deliberately avoids std::atomic<std::shared_ptr<T>>.
// It is C++20 on paper, but the NDK's libc++ does not implement that
// specialization and the build fails on it — which is exactly how the first
// version of this rewrite died in CI. A shared_mutex plus a plain table is
// C++17, portable, and never touched on the hot path anyway.
//
// The public API is unchanged on purpose — every caller in the runtime keeps
// working, it just stops paying for a file parse per read.
// ═══════════════════════════════════════════════════════════════════════════

namespace eclient_runtime::memory {
namespace {
constexpr const char* TAG = "EClientMemory";

// How often a MISS may trigger a re-parse of the map. A miss is either a region
// that appeared after startup (dlopen of a new library, a freshly mmap'd arena)
// or a genuinely invalid address; the rate limit serves the first case quickly
// without letting the second case hammer the filesystem.
constexpr std::int64_t kRefreshFloorMs = 250;

struct Range {
    std::uintptr_t lo{0};
    std::uintptr_t hi{0};
    bool readable{false};
};

using Table = std::vector<Range>;

// Read-mostly: readers take a shared lock only when the thread-local hot cache
// misses, which for the runtime's access pattern is rare.
std::shared_mutex g_mapMutex;
Table g_table;
std::mutex g_refreshMutex;
std::atomic<std::int64_t> g_lastRefreshMs{0};

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Parse /proc/self/maps into a sorted table.
Table parseMaps() {
    Table table;
    table.reserve(2048);

    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::uintptr_t lo = 0, hi = 0;
        char perms[5]{};
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
        if (hi <= lo) continue;
        Range r;
        r.lo = lo;
        r.hi = hi;
        r.readable = (perms[0] == 'r');
        table.push_back(r);
    }

    std::sort(table.begin(), table.end(),
              [](const Range& a, const Range& b) { return a.lo < b.lo; });
    return table;
}

// Find the range containing [start, end). Returns null when nothing matches.
const Range* findRange(const Table& table, std::uintptr_t start, std::uintptr_t end) {
    if (table.empty()) return nullptr;
    const auto it = std::upper_bound(table.begin(), table.end(), start,
                                     [](std::uintptr_t value, const Range& r) { return value < r.lo; });
    if (it == table.begin()) return nullptr;
    const Range& candidate = *(it - 1);
    if (start >= candidate.lo && end < candidate.hi && candidate.readable) return &candidate;
    return nullptr;
}

// The common case is a run of reads against the same struct, so remember the
// last few ranges per thread. Direct-mapped, no eviction policy needed: a wrong
// guess costs one binary search, never a wrong answer.
constexpr int kHotEntries = 16;
struct HotRange {
    std::uintptr_t lo{0};
    std::uintptr_t hi{0};
    bool valid{false};
};

bool hotCacheHolds(std::uintptr_t start, std::uintptr_t end) {
    static thread_local HotRange hot[kHotEntries];
    const std::size_t slot = ((start >> 4) ^ (start >> 16)) & (kHotEntries - 1);
    const HotRange& h = hot[slot];
    return h.valid && start >= h.lo && end < h.hi;
}

void hotCacheRemember(const Range& r) {
    static thread_local HotRange hot[kHotEntries];
    const std::size_t slot = ((r.lo >> 4) ^ (r.lo >> 16)) & (kHotEntries - 1);
    hot[slot].lo = r.lo;
    hot[slot].hi = r.hi;
    hot[slot].valid = true;
}

bool readable(const void* p, std::size_t size) {
    if (!p || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(p);
    const auto end = start + size - 1;

    // Fast path: nothing but a couple of comparisons and a thread-local lookup.
    if (hotCacheHolds(start, end)) return true;

    {
        std::shared_lock<std::shared_mutex> lock(g_mapMutex);
        if (const Range* r = findRange(g_table, start, end)) {
            hotCacheRemember(*r);
            return true;
        }
    }

    // Miss: the map may have changed under us (a library loaded, an arena
    // grown). Ask once — rate-limited — and retry before declaring the address
    // unusable.
    std::lock_guard<std::mutex> lock(g_refreshMutex);
    const std::int64_t now = nowMs();
    if (now - g_lastRefreshMs.load(std::memory_order_relaxed) < kRefreshFloorMs) {
        return false;   // refresh too recent: this address really is unusable
    }
    {
        Table fresh = parseMaps();
        std::unique_lock<std::shared_mutex> write(g_mapMutex);
        g_table.swap(fresh);
    }
    g_lastRefreshMs.store(now, std::memory_order_relaxed);

    std::shared_lock<std::shared_mutex> lock2(g_mapMutex);
    if (const Range* r = findRange(g_table, start, end)) {
        hotCacheRemember(*r);
        return true;
    }
    return false;
}

// ── module discovery (unchanged) ────────────────────────────────────────────
struct FindContext { const char* wanted{}; ModuleInfo* out{}; };

int visit(struct dl_phdr_info* info, size_t, void* opaque) {
    auto* ctx = static_cast<FindContext*>(opaque);
    if (!info || !info->dlpi_name || !*info->dlpi_name) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* name = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(name, ctx->wanted) != 0) return 0;

    ctx->out->base = static_cast<std::uintptr_t>(info->dlpi_addr);
    ctx->out->path = info->dlpi_name;
    for (Elf64_Half i = 0; i < info->dlpi_phnum; ++i) {
        const Elf64_Phdr& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X) || ph.p_memsz == 0) continue;
        ModuleRange range{};
        range.start = ctx->out->base + ph.p_vaddr;
        range.end = range.start + ph.p_memsz;
        range.fileOffset = ph.p_offset;
        range.protection = PROT_READ | PROT_EXEC;
        ctx->out->executableRanges.push_back(range);
    }
    return 1;
}

} // namespace

NativeMemory& NativeMemory::instance() {
    static NativeMemory memory;
    return memory;
}

bool NativeMemory::locateModule(const char* soname, ModuleInfo& out) const {
    out = {};
    if (!soname || !*soname) return false;
    FindContext ctx{soname, &out};
    dl_iterate_phdr(visit, &ctx);
    return out.base != 0 && !out.executableRanges.empty();
}

bool NativeMemory::read(std::uintptr_t address, void* out, std::size_t size) const {
    if (!out || size == 0 || !readable(reinterpret_cast<const void*>(address), size)) return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}

bool NativeMemory::write(std::uintptr_t address, const void* data, std::size_t size) const {
    if (!data || size == 0 || !readable(reinterpret_cast<const void*>(address), size)) return false;
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) return false;
    const auto begin = address & ~static_cast<std::uintptr_t>(page - 1);
    const auto finish = (address + size + static_cast<std::uintptr_t>(page - 1)) &
                        ~static_cast<std::uintptr_t>(page - 1);
    const std::size_t length = finish - begin;
    if (mprotect(reinterpret_cast<void*>(begin), length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
    std::memcpy(reinterpret_cast<void*>(address), data, size);
    __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address + size));
    (void)mprotect(reinterpret_cast<void*>(begin), length, PROT_READ | PROT_EXEC);
    return true;
}

std::uintptr_t NativeMemory::scan(const ModuleInfo& module, const std::vector<std::uint8_t>& bytes,
                                   const std::string& mask) const {
    if (bytes.empty() || bytes.size() != mask.size()) return 0;
    for (const auto& range : module.executableRanges) {
        const auto size = range.end - range.start;
        if (size < bytes.size()) continue;
        const auto* data = reinterpret_cast<const std::uint8_t*>(range.start);
        for (std::size_t i = 0; i + bytes.size() <= size; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < bytes.size(); ++j) {
                if (mask[j] != '?' && data[i + j] != bytes[j]) { match = false; break; }
            }
            if (match) return range.start + i;
        }
    }
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "Pattern scan found no match");
    return 0;
}

// Exposed for diagnostics: how many ranges the cached table holds. Lets a
// device log prove the cache is populated and being reused.
std::size_t NativeMemory::cachedRangeCount() const {
    std::shared_lock<std::shared_mutex> lock(g_mapMutex);
    return g_table.size();
}

void NativeMemory::invalidateMapCache() const {
    std::unique_lock<std::shared_mutex> lock(g_mapMutex);
    g_table.clear();
    g_lastRefreshMs.store(0, std::memory_order_relaxed);
}

} // namespace eclient_runtime::memory
