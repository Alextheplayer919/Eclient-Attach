// Host implementation of the one Android symbol the runtime actually calls.
#include <android/log.h>
#include <cstdarg>
#include <cstdio>

extern "C" int __android_log_print(int, const char* tag, const char* fmt, ...) {
    std::printf("[%s] ", tag);
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    return 0;
}

#include "modules/ModuleManager.h"
namespace eclient_runtime::kotlinbridge {
void setModuleEnabled(const char*, bool) {}
} // namespace eclient_runtime::kotlinbridge

// ── seams the tick bench does not exercise ────────────────────────────────
// DebugFiles (log tee) and the Kotlin dispatch are device-side; the bench
// stubs them so the module code links without pulling in jni.h.
#include <cstddef>
#include "bridge/DebugFiles.h"
namespace eclient_runtime::debugfiles {
PublicLog::~PublicLog() = default;
bool PublicLog::open(const char*) { return false; }   // no file sinks on the host
void PublicLog::write(const char*) {}
void PublicLog::close() {}
} // namespace eclient_runtime::debugfiles

namespace eclient_runtime::kotlinbridge {
bool anyKotlinModuleEnabled() { return false; }
void dispatchTick(void*) {}
} // namespace eclient_runtime::kotlinbridge
