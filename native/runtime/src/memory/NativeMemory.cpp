#include "memory/NativeMemory.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace eclient_runtime::memory {
namespace {
constexpr const char* TAG = "EClientMemory";

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

bool readable(const void* p, std::size_t size) {
    if (!p || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(p);
    const auto end = start + size - 1;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::uintptr_t lo = 0, hi = 0;
        char perms[5]{};
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
        if (start >= lo && end < hi && perms[0] == 'r') return true;
    }
    return false;
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

} // namespace eclient_runtime::memory
