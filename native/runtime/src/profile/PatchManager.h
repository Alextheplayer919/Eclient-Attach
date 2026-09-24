#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "memory/NativeMemory.h"

namespace eclient_runtime::profile {
struct PatchSpec {
    const char* module;
    std::uint64_t offset;
    std::vector<std::uint8_t> expected;
    std::vector<std::uint8_t> enabled;
};

class PatchManager final {
public:
    static PatchManager& instance();
    bool initialize();
    bool set(const std::string& module, bool enabled);
    bool enabled(const std::string& module) const;
    bool ready() const;
    std::string status() const;
private:
    PatchManager() = default;
    std::vector<PatchSpec> specs_;
    std::vector<bool> active_;
    memory::ModuleInfo minecraft_{};
    bool ready_{false};
    std::string status_{};
};
}
