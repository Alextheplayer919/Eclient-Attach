#include "host/MinecraftHost.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <EGL/egl.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "gui/InGameGui.h"
#include "../bridge/GameBridge.h"
#include "../input/NativeInputHook.h"
#include "../profile/PatchManager.h"
#include "../modules/GameplayModules.h"

namespace eclient_runtime::host {
namespace {

constexpr const char* TAG =
    "EClientHost";

constexpr const char* TARGET =
    "libminecraftpe.so";

struct FindResult {
    std::uintptr_t base{};
    const char* path{};
};

int findMinecraft(
    struct dl_phdr_info* info,
    size_t,
    void* opaque) {

    auto* result =
        static_cast<FindResult*>(opaque);

    if (!info ||
        !info->dlpi_name ||
        !*info->dlpi_name) {
        return 0;
    }

    const char* slash =
        std::strrchr(
            info->dlpi_name,
            '/');

    const char* name =
        slash
            ? slash + 1
            : info->dlpi_name;

    if (std::strcmp(name, TARGET) != 0) {
        return 0;
    }

    result->base =
        static_cast<std::uintptr_t>(
            info->dlpi_addr);

    result->path =
        info->dlpi_name;

    return 1;
}

} // namespace

MinecraftHost& MinecraftHost::instance() {
    static MinecraftHost host;
    return host;
}

void MinecraftHost::start() {
    if (running_.exchange(true)) {
        return;
    }

    {
        std::lock_guard lock(mutex_);

        snapshot_.started = true;

        snapshot_.status =
            "waiting for Minecraft runtime";
    }

    thread_ =
        std::thread(
            &MinecraftHost::bootstrapLoop,
            this);
}

void MinecraftHost::stop() {
    running_.store(false);

    if (thread_.joinable()) {
        thread_.join();
    }

    InGameGui::shutdown();

    std::lock_guard lock(mutex_);

    snapshot_.renderHookReady = false;
    snapshot_.guiReady = false;
    snapshot_.versionMatched = false;

    snapshot_.status =
        "stopped";
}

HostSnapshot MinecraftHost::snapshot() const {
    std::lock_guard lock(mutex_);

    return snapshot_;
}

bool MinecraftHost::discoverMinecraft() {
    FindResult result{};

    dl_iterate_phdr(
        findMinecraft,
        &result);

    if (!result.base) {
        return false;
    }

    std::lock_guard lock(mutex_);

    snapshot_.minecraftLoaded = true;

    snapshot_.minecraftBase =
        result.base;

    snapshot_.minecraftPath =
        result.path
            ? result.path
            : TARGET;

    return true;
}

bool MinecraftHost::verifyTarget() {
    // The supplied Minecraft 1.21.111 ARM64
    // libminecraftpe.so does not expose a readable
    // GNU build-id.
    //
    // GameBridge therefore validates the actual
    // loaded module and its executable ranges.
    //
    // Do not compare against the external build ID
    // here because doing so would reject the correct
    // supplied binary.

    const auto bridge =
        GameBridge::instance().snapshot();

    const bool matched =
        bridge.libraryLoaded &&
        bridge.fingerprintMatched &&
        bridge.symbolsReady &&
        bridge.moduleBase != 0 &&
        bridge.executableRangeCount != 0;

    std::lock_guard lock(mutex_);

    snapshot_.buildId =
        bridge.buildId;

    snapshot_.minecraftBase =
        bridge.moduleBase;

    snapshot_.minecraftPath =
        bridge.libraryPath;

    snapshot_.versionMatched =
        matched;

    if (!matched) {

        snapshot_.status =
            "Minecraft loaded, but native 1.21.111 module validation is incomplete";

        return false;
    }

    snapshot_.status =
        "1.21.111 ARM64 runtime validated";

    return true;
}

bool MinecraftHost::installRenderHook() {

    if (!InGameGui::initialize()) {
        return false;
    }

    if (!eclient_runtime::input::initialize()) {

        __android_log_print(
            ANDROID_LOG_WARN,
            TAG,
            "No input hook available; GUI will render but cannot be toggled/touched");
    }

    std::lock_guard lock(mutex_);

    snapshot_.guiReady = true;
    snapshot_.renderHookReady = true;

    snapshot_.status =
        "E-Client host initialized inside Minecraft process";

    return true;
}

void MinecraftHost::updateStatus(
    const char* status) {

    std::lock_guard lock(mutex_);

    snapshot_.status =
        status
            ? status
            : "unknown";
}

void MinecraftHost::bootstrapLoop() {

    __android_log_print(
        ANDROID_LOG_INFO,
        TAG,
        "Host bootstrap started");

    while (running_.load()) {

        if (!discoverMinecraft()) {

            std::this_thread::sleep_for(
                std::chrono::milliseconds(250));

            continue;
        }

        GameBridge::instance()
            .initialize();

        if (!verifyTarget()) {

            updateStatus(
                "Minecraft runtime found; waiting for validated 1.21.111 ARM64 profile");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            continue;
        }

        if (!eclient_runtime::profile::PatchManager::instance().initialize()) {

            updateStatus(
                "1.21.111 runtime found, but patch profile validation failed");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            continue;
        }

        if (!eclient_runtime::modules::gameplay::initialize()) {

            updateStatus(
                "1.21.111 patch profile ready, gameplay hook installation failed");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            continue;
        }

        if (installRenderHook()) {

            __android_log_print(
                ANDROID_LOG_INFO,
                TAG,
                "E-Client 1.21.111 in-game host ready");

            return;
        }

        updateStatus(
            "GUI initialization failed");

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));
    }
}

} // namespace eclient_runtime::host
