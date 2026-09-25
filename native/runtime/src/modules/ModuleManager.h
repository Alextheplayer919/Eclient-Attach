#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace eclient_runtime::modules {

// A typed, Melody-style module setting (Bool/Int/Float/Enum) that renders in
// the ClickGUI under its module. Defaults are chosen to match the Melody
// module each setting was ported from.
struct Setting {
    enum class Type { Bool, Int, Float, Enum };

    std::string key;          // stable programmatic key, e.g. "Range"
    std::string label;        // GUI label (== key by default)
    std::string tooltip;      // extra note shown on hover ("pending: ..." etc.)
    Type type{Type::Bool};

    // Bool
    bool boolValue{false};
    bool boolDefault{false};

    // Int
    int intValue{0};
    int intDefault{0};
    int intMin{0};
    int intMax{0};

    // Float
    float floatValue{0.0f};
    float floatDefault{0.0f};
    float floatMin{0.0f};
    float floatMax{0.0f};

    // Enum
    std::vector<std::string> options;
    int enumValue{0};
    int enumDefault{0};

    static Setting boolean(std::string key, bool def, std::string tooltip = {});
    static Setting integer(std::string key, int def, int min, int max, std::string tooltip = {});
    static Setting real(std::string key, float def, float min, float max, std::string tooltip = {});
    static Setting choice(std::string key, std::vector<std::string> options, int def, std::string tooltip = {});
};

struct ModuleState {
    std::string name;
    std::string description;
    std::string category;   // Combat | Movement | Visual | Player | Misc
    bool enabled{false};
    bool memoryBacked{false};
    std::vector<Setting> settings;
    // Implementation lives in the dex-compiled Kotlin layer (kotlin-modules/):
    // the native toggle is mirrored into the Kotlin ModuleRegistry via the
    // KotlinBridge and its tick is dispatched from the melody tick.
    bool kotlin{false};
};

class ModuleManager final {
public:
    static ModuleManager& instance();
    const std::vector<ModuleState>& all() const;
    bool setEnabled(const std::string& name, bool enabled);
    bool enabled(const std::string& name) const;

    // Setting reads — cheap, used by module tick logic on the game thread.
    bool boolSetting(const std::string& module, const std::string& key, bool fallback) const;
    int intSetting(const std::string& module, const std::string& key, int fallback) const;
    float floatSetting(const std::string& module, const std::string& key, float fallback) const;
    int enumSetting(const std::string& module, const std::string& key, int fallback) const;

    // Setting writes from the GUI. Returns false if module/key missing or the
    // value is out of range (the GUI then keeps showing the previous value).
    bool setBoolSetting(const std::string& module, const std::string& key, bool value);
    bool setIntSetting(const std::string& module, const std::string& key, int value);
    bool setFloatSetting(const std::string& module, const std::string& key, float value);
    bool setEnumSetting(const std::string& module, const std::string& key, int index);

private:
    ModuleManager();
    Setting* findSetting(const std::string& module, const std::string& key);
    const Setting* findSetting(const std::string& module, const std::string& key) const;
    mutable std::mutex mutex_;
    std::vector<ModuleState> modules_;
};

} // namespace eclient_runtime::modules
