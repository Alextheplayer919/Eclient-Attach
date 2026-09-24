#include "input/NativeInputHook.h"

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>

#include "dobby.h"
#include "gui/InGameGui.h"
#include "bridge/GameBridge.h"

namespace eclient_runtime::input {
namespace {
constexpr const char* TAG = "EClientInput";
using NativeKeyHandler = bool (*)(void*, void*, int, int);
using GetEventFn = int32_t (*)(AInputQueue*, AInputEvent**);

std::atomic_bool g_keyInstalled{false};
std::atomic_bool g_touchInstalled{false};
NativeKeyHandler g_original = nullptr;
GetEventFn g_originalGetEvent = nullptr;

bool hookedNativeKeyHandler(void* a1, void* a2, int keyCode, int keyAction) {
    // Volume-up toggles the in-game menu (ACTION_DOWN only).
    if (keyCode == AKEYCODE_VOLUME_UP && keyAction == 0) {
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    return g_original ? g_original(a1, a2, keyCode, keyAction) : false;
}

// While the menu is open, motion events are routed to ImGui and consumed so the
// game camera does not move behind the menu.
int32_t hookedGetEvent(AInputQueue* queue, AInputEvent** outEvent) {
    const int32_t result = g_originalGetEvent ? g_originalGetEvent(queue, outEvent) : -1;
    if (result < 0 || !outEvent || !*outEvent) return result;
    if (!eclient_runtime::host::InGameGui::menuOpen()) return result;

    AInputEvent* ev = *outEvent;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return result;

    const int action = AMotionEvent_getAction(ev);
    const int masked = action & AMOTION_EVENT_ACTION_MASK;
    const int pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    // Prefer the active pointer for multi-touch; fall back to index 0.
    size_t idx = 0;
    if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
        masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        idx = static_cast<size_t>(pointerIndex);
    }
    if (idx >= AMotionEvent_getPointerCount(ev)) idx = 0;

    const float x = AMotionEvent_getX(ev, idx);
    const float y = AMotionEvent_getY(ev, idx);

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 0);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 1);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 2);
            break;
        default:
            break;
    }

    // Consume so Minecraft does not also process the gesture.
    AInputQueue_finishEvent(queue, ev, 1);
    *outEvent = nullptr;
    return -1;
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

bool installTouchHook() {
    if (g_touchInstalled.load()) return true;
    void* symbol = dlsym(RTLD_DEFAULT, "AInputQueue_getEvent");
    if (!symbol) symbol = reinterpret_cast<void*>(&AInputQueue_getEvent);
    if (!symbol) return false;
    if (DobbyHook(symbol, reinterpret_cast<void*>(hookedGetEvent),
                  reinterpret_cast<void**>(&g_originalGetEvent)) != RS_SUCCESS ||
        !g_originalGetEvent) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "AInputQueue_getEvent hook failed");
        g_originalGetEvent = nullptr;
        return false;
    }
    g_touchInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Touch routing for the GUI ready");
    return true;
}
} // namespace

bool initialize() {
    const bool key = installKeyHook();
    const bool touch = installTouchHook();
    if (!key) {
        // No hotkey: open the menu so it remains reachable.
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    if (!touch) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "Touch hook failed — GUI will show but may not receive taps");
    }
    return key || touch;
}

void shutdown() {
    g_keyInstalled.store(false);
    g_touchInstalled.store(false);
}

} // namespace eclient_runtime::input
