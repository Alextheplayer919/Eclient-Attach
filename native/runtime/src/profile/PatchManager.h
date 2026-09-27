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
    /// Apply a patch group. `why` receives the reason when this returns false —
    /// "no patch profile for X" and "the profile failed validation" are very
    /// different problems and the UI must not conflate them.
    bool set(const std::string& module, bool enabled, std::string* why = nullptr);
    bool enabled(const std::string& module) const;
    bool ready() const;
    std::string status() const;

    /// Does a patch group with this exact name exist? Spec names are the JOIN
    /// KEY to ModuleManager's module names, so this is how a name typo gets
    /// caught instead of silently refusing every toggle.
    bool hasSpec(const std::string& module) const;
    std::size_t specCount() const;
private:
    PatchManager() = default;
    std::vector<PatchSpec> specs_;
    std::vector<bool> active_;
    memory::ModuleInfo minecraft_{};
    bool ready_{false};
    std::string status_{};
};
}
