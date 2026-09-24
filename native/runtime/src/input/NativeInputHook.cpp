#include "input/NativeInputHook.h"

#include <android/keycodes.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <atomic>

#include "dobby.h"
#include "gui/InGameGui.h"
#include "bridge/GameBridge.h"

namespace eclient_runtime::input {
namespace {
constexpr const char* TAG = "EClientInput";
using NativeKeyHandler = bool (*)(void*, void*, int, int);

// Volume-key hook state (this path is verified working on 1.21.111:
// Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler is a real export).
std::atomic_bool g_keyInstalled{false};
NativeKeyHandler g_original = nullptr;

// TOUCH ROUTE (fixed 2026-09-24): libminecraftpe.so 1.21.111 imports NO
// AInputQueue_* symbols at all, so the old AInputQueue_getEvent Dobby hook
// could never observe a single touch — the GUI looked dead. Touches are now
// delivered by a view-layer OnTouchListener (see
// tools/minecraft-touch-listener.smali) calling into nativeGuiTouch below;
// g_touchInstalled flips when the listener is actually installed on a resumed
// activity window (nativeTouchReady callback).
std::atomic_bool g_touchInstalled{false};
std::atomic<long> g_guiTouchCount{0};

bool hookedNativeKeyHandler(void* a1, void* a2, int keyCode, int keyAction) {
    // Volume-up toggles the in-game menu (ACTION_DOWN only).
    if (keyCode == AKEYCODE_VOLUME_UP && keyAction == 0) {
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    return g_original ? g_original(a1, a2, keyCode, keyAction) : false;
}

void* findInMinecraft(const char* symbol) {
    const auto snap = eclient_runtime::GameBridge::instance().snapshot();
    const char* candidates[2] = {snap.libraryPath.empty() ? nullptr : snap.libraryPath.c_str(),
                                 "libminecraftpe.so"};
    for (const char* lib : candidates) {
        if (!lib) continue;
        void* handle = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
        if (!handle) continue;
        void* sym = dlsym(handle, symbol);
        dlclose(handle);
        if (sym) return sym;
    }
    return dlsym(RTLD_DEFAULT, symbol);
}

bool installKeyHook() {
    if (g_keyInstalled.load()) return true;
    void* symbol = findInMinecraft("Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler");
    if (!symbol) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "nativeKeyHandler export not found");
        return false;
    }
    if (DobbyHook(symbol, reinterpret_cast<void*>(hookedNativeKeyHandler),
                  reinterpret_cast<void**>(&g_original)) != RS_SUCCESS || !g_original) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "nativeKeyHandler hook failed");
        g_original = nullptr;
        return false;
    }
    g_keyInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Mobile GUI hotkey ready: Volume Up");
    return true;
}
} // namespace

bool initialize() {
    const bool key = installKeyHook();
    if (!key) {
        // No hotkey: open the menu so it remains reachable.
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    return key;
}

void shutdown() {
    g_keyInstalled.store(false);
    g_touchInstalled.store(false);
}

bool keyHookInstalled() { return g_keyInstalled.load(); }
bool touchHookInstalled() { return g_touchInstalled.load(); }

} // namespace eclient_runtime::input

// ---------------------------------------------------------------------------
// JNI entry points called from the injected smali (resolved by NAME at load).
// Keep signatures in exact sync with tools/minecraft-host-loader.smali and
// tools/minecraft-touch-listener.smali — the smali is ABI here.
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_rubidiumclient_host_EClientLoaderProvider_nativeTouchReady(JNIEnv*, jclass) {
    eclient_runtime::input::g_touchInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, "EClientInput",
        "View-layer touch listener installed on a resumed activity window");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_rubidiumclient_host_EClientLoaderProvider_nativeGuiTouch(
        JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    namespace gui = eclient_runtime::host::InGameGui;
    if (!gui::menuOpen()) {
        return JNI_FALSE;  // forward into the app, game sees input normally
    }

    // MotionEvent action-masked -> InGameGui touch codes (0=down, 1=move, 2=up)
    int mapped;
    switch (action) {
        case 0: /* ACTION_DOWN */          mapped = 0; break;
        case 2: /* ACTION_MOVE */          mapped = 1; break;
        case 1: /* ACTION_UP */
        case 3: /* ACTION_CANCEL */        mapped = 2; break;
        default: /* multi-touch variants */ mapped = 1; break;
    }
    gui::submitTouch(x, y, mapped);

    const long total = ++eclient_runtime::input::g_guiTouchCount;
    if (total == 1 || total % 100 == 0) {
        __android_log_print(ANDROID_LOG_INFO, "EClientInput",
            "GUI touch events flowing (%ld)", total);
    }
    return JNI_TRUE;  // consumed while the menu is open
}
