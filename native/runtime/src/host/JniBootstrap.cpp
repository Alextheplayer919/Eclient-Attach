#include "host/MinecraftHost.h"
#include <jni.h>
#include <android/log.h>

namespace {
constexpr const char* TAG = "EClientJNI";
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    (void)vm;
    __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client native library loaded by host process");
    eclient_runtime::host::MinecraftHost::instance().start();
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_rubidiumclient_core_runtime_NativeRuntimeBridge_nativeHostStop(JNIEnv*, jclass) {
    eclient_runtime::host::MinecraftHost::instance().stop();
}
