#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace eclient_runtime::host {

struct HostSnapshot {
    bool started{false};
    bool minecraftLoaded{false};
    bool versionMatched{false};
    bool renderHookReady{false};
    bool guiReady{false};
    std::uintptr_t minecraftBase{0};
    std::string minecraftPath{};
    std::string buildId{};
    std::string status{"not started"};
};

class MinecraftHost final {
public:
    static MinecraftHost& instance();

    void start();
    void stop();
    HostSnapshot snapshot() const;

private:
    MinecraftHost() = default;
    MinecraftHost(const MinecraftHost&) = delete;
    MinecraftHost& operator=(const MinecraftHost&) = delete;

    void bootstrapLoop();
    bool discoverMinecraft();
    bool verifyTarget();
    bool installRenderHook();
    void updateStatus(const char* status);

    mutable std::mutex mutex_;
    HostSnapshot snapshot_{};
    std::atomic_bool running_{false};
    std::thread thread_{};
};

} // namespace eclient_runtime::host
