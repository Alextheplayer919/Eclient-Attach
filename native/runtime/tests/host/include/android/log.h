#pragma once
// Host stub for <android/log.h> so the runtime's non-Android TUs can be built
// and exercised on a plain Linux box (no NDK, no device).
#include <cstdarg>
enum {
    ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT = 1, ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6, ANDROID_LOG_FATAL = 7, ANDROID_LOG_SILENT = 8,
};
extern "C" int __android_log_print(int prio, const char* tag, const char* fmt, ...);
