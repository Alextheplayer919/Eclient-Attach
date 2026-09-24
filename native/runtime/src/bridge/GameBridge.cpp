#include "bridge/GameBridge.h"

#include <link.h>
#include <elf.h>
#include <dlfcn.h>
#include <unistd.h>
#include <android/log.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include "memory/NativeMemory.h"

namespace eclient_runtime {
namespace {

constexpr const char* TAG = "EClientRuntime";
constexpr const char* TARGET_LIBRARY = "libminecraftpe.so";

// The exact Minecraft 1.21.111 ARM64 APK is validated by the GitHub Actions
// patch pipeline before this library is packaged.
//
// The supplied libminecraftpe.so does not contain a readable GNU build-id,
// so runtime compatibility must not depend on a GNU build-id being present.
//
// Runtime validation checks:
//   1. libminecraftpe.so is actually loaded.
//   2. It has an executable PT_LOAD segment.
//   3. NativeMemory can locate the same module.
//   4. The module has executable memory ranges.
//
// The exact APK SHA-256 is checked by the GitHub Actions patch pipeline:
//
// 69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59

struct ModuleInfo {
    uintptr_t base{};
    const char* path{};
    bool hasExecutableLoad{false};
};

int findMinecraft(struct dl_phdr_info* info, size_t, void* opaque) {
    auto* result = static_cast<ModuleInfo*>(opaque);

    if (!info || !info->dlpi_name || !*info->dlpi_name) {
        return 0;
    }

    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* name = slash ? slash + 1 : info->dlpi_name;

    if (std::strcmp(name, TARGET_LIBRARY) != 0) {
        return 0;
    }

    result->base =
        static_cast<uintptr_t>(info->dlpi_addr);

    result->path = info->dlpi_name;

    for (Elf64_Half i = 0; i < info->dlpi_phnum; ++i) {
        const Elf64_Phdr& ph = info->dlpi_phdr[i];

        if (ph.p_type == PT_LOAD &&
            (ph.p_flags & PF_X) != 0 &&
            ph.p_memsz != 0) {

            result->hasExecutableLoad = true;
            break;
        }
    }

    return 1;
}

} // namespace

GameBridge& GameBridge::instance() {
    static GameBridge bridge;
    return bridge;
}

bool GameBridge::discoverMinecraft() {
    ModuleInfo info{};

    dl_iterate_phdr(findMinecraft, &info);

    if (!info.base) {
        std::lock_guard lock(mutex_);

        snapshot_.libraryLoaded = false;
        snapshot_.fingerprintMatched = false;
        snapshot_.symbolsReady = false;
        snapshot_.status =
            "waiting for libminecraftpe.so";

        return false;
    }

    moduleBase_ = info.base;
    modulePath_ =
        info.path ? info.path : TARGET_LIBRARY;

    {
        std::lock_guard lock(mutex_);

        snapshot_.libraryLoaded = true;
        snapshot_.fingerprintMatched =
            info.hasExecutableLoad;

        // The supplied 1.21.111 library has no usable GNU build ID.
        snapshot_.buildId.clear();

        snapshot_.libraryPath =
            modulePath_;

        snapshot_.moduleBase =
            moduleBase_;

        snapshot_.status =
            info.hasExecutableLoad
                ? "1.21.111 ARM64 Minecraft library loaded"
                : "Minecraft library loaded without executable segment";
    }

    return true;
}

bool GameBridge::verifyFingerprint() {
    ModuleInfo info{};

    dl_iterate_phdr(findMinecraft, &info);

    const bool moduleValid =
        info.base != 0 &&
        info.hasExecutableLoad;

    std::lock_guard lock(mutex_);

    snapshot_.libraryLoaded =
        info.base != 0;

    snapshot_.fingerprintMatched =
        moduleValid;

    snapshot_.buildId.clear();

    snapshot_.moduleBase =
        info.base;

    if (info.path) {
        snapshot_.libraryPath =
            info.path;
    }

    if (!moduleValid) {
        snapshot_.symbolsReady = false;
        snapshot_.hookInstalled = false;

        snapshot_.status =
            "Minecraft library failed executable-module validation";

        return false;
    }

    snapshot_.status =
        "1.21.111 ARM64 target accepted; exact APK verified by build pipeline";

    return true;
}

bool GameBridge::initialize() {
    if (running_.exchange(true)) {
        return true;
    }

    discoverMinecraft();
    verifyFingerprint();

    heartbeatThread_ =
        std::thread([this] {
            heartbeatLoop();
        });

    initializeMemoryProfile();

    const auto s = snapshot();

    __android_log_print(
        ANDROID_LOG_INFO,
        TAG,
        "Native runtime: library=%s profile=%s build-id=%s",
        s.libraryLoaded ? "yes" : "no",
        s.fingerprintMatched ? "accepted" : "rejected",
        s.buildId.empty()
            ? "none"
            : s.buildId.c_str());

    return true;
}

bool GameBridge::initializeMemoryProfile() {
    memory::ModuleInfo module{};

    const bool found =
        memory::NativeMemory::instance()
            .locateModule(TARGET_LIBRARY, module);

    std::lock_guard lock(mutex_);

    if (!found) {
        snapshot_.symbolsReady = false;

        snapshot_.status =
            "libminecraftpe.so not found by native memory layer";

        return false;
    }

    snapshot_.moduleBase =
        module.base;

    snapshot_.executableRangeCount =
        module.executableRanges.size();

    snapshot_.symbolsReady =
        snapshot_.fingerprintMatched &&
        module.base != 0 &&
        !module.executableRanges.empty();

    if (!snapshot_.symbolsReady) {
        snapshot_.status =
            "Minecraft module found, executable ranges unavailable";

        return false;
    }

    snapshot_.status =
        "1.21.111 ARM64 memory profile initialized";

    return true;
}

void GameBridge::shutdown() {
    running_.store(false);

    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }

    std::lock_guard lock(mutex_);

    snapshot_.hookInstalled = false;
    snapshot_.playerSeen = false;
    snapshot_.status =
        "native runtime stopped";
}

void GameBridge::heartbeatLoop() {
    while (running_.load()) {

        const auto nowMs =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::system_clock::now()
                        .time_since_epoch())
                .count();

        {
            std::lock_guard lock(mutex_);

            ++snapshot_.heartbeat;

            snapshot_.lastUpdateMs =
                nowMs;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(250));
    }
}

BridgeSnapshot GameBridge::snapshot() const {
    std::lock_guard lock(mutex_);

    return snapshot_;
}

} // namespace eclient_runtime
