#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace eclient_runtime::modules {

struct ModuleState {
    std::string name;
    std::string description;
    std::string category;   // Combat | Movement | Visual | Player | Misc
    bool enabled{false};
    bool memoryBacked{false};
};

class ModuleManager final {
public:
    static ModuleManager& instance();
    const std::vector<ModuleState>& all() const;
    bool setEnabled(const std::string& name, bool enabled);
    bool enabled(const std::string& name) const;

private:
    ModuleManager();
    mutable std::mutex mutex_;
    std::vector<ModuleState> modules_;
};

} // namespace eclient_runtime::modules
