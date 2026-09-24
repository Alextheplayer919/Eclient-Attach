#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eclient_runtime::memory {

struct ModuleRange {
    std::uintptr_t start{};
    std::uintptr_t end{};
    std::uintptr_t fileOffset{};
    int protection{};
};

struct ModuleInfo {
    std::uintptr_t base{};
    std::string path{};
    std::string buildId{};
    std::vector<ModuleRange> executableRanges{};
};

class NativeMemory final {
public:
    static NativeMemory& instance();

    bool locateModule(const char* soname, ModuleInfo& out) const;
    bool read(std::uintptr_t address, void* out, std::size_t size) const;
    bool write(std::uintptr_t address, const void* data, std::size_t size) const;
    std::uintptr_t scan(const ModuleInfo& module, const std::vector<std::uint8_t>& bytes,
                        const std::string& mask) const;

private:
    NativeMemory() = default;
    NativeMemory(const NativeMemory&) = delete;
    NativeMemory& operator=(const NativeMemory&) = delete;
};

} // namespace eclient_runtime::memory
