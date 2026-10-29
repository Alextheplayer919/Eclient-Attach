#include "input/NativeInputHook.h"

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <unistd.h>

#include "dobby.h"
#include "imgui.h"
#include "gui/InGameGui.h"

static void inputLog(const char* msg) {
    __android_log_print(ANDROID_LOG_ERROR, "EClientInput", "%s", msg);
    FILE* f = fopen("/sdcard/Download/EClient_input.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static int g_inputCount = 0;
#include "bridge/GameBridge.h"

namespace eclient_runtime::input {
namespace {
constexpr const char* TAG = "EClientInput";
using NativeKeyHandler = bool (*)(void*, void*, int, int);
using GetEventFn = int32_t (*)(AInputQueue*, AInputEvent**);
using PreDispatchFn = int32_t (*)(AInputQueue*, AInputEvent*);

std::atomic_bool g_keyInstalled{false};
std::atomic_bool g_touchInstalled{false};
NativeKeyHandler g_original = nullptr;
GetEventFn g_originalGetEvent = nullptr;
PreDispatchFn g_originalPreDispatch = nullptr;

bool hookedNativeKeyHandler(void* a1, void* a2, int keyCode, int keyAction) {
    // No volume-up toggle — GUI is persistent with minimize button.
    // Still pass through to game so volume keys work normally.
    return g_original ? g_original(a1, a2, keyCode, keyAction) : false;
}

// While GUI is hit, motion events are routed to ImGui and consumed so the
// game camera does not move behind the menu. Otherwise they pass through.
int32_t hookedPreDispatch(AInputQueue* queue, AInputEvent* ev) {
    if (ev && AInputEvent_getType(ev)==AINPUT_EVENT_TYPE_MOTION) {
        const int act = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(ev,0), y=AMotionEvent_getY(ev,0);
        if (g_inputCount < 30) {
            char b[140]; snprintf(b,sizeof(b),"PRE-DISPATCH act=%d x=%.0f y=%.0f onGui=%d", act, x, y, eclient_runtime::host::InGameGui::isTouchOnGui(x,y)?1:0);
            inputLog(b);
        }
        // also forward to ImGui for safety (duplicate is okay)
        if (act==AMOTION_EVENT_ACTION_DOWN) eclient_runtime::host::InGameGui::submitTouch(x,y,0);
        else if (act==AMOTION_EVENT_ACTION_MOVE) eclient_runtime::host::InGameGui::submitTouch(x,y,1);
        else if (act==AMOTION_EVENT_ACTION_UP || act==AMOTION_EVENT_ACTION_CANCEL) eclient_runtime::host::InGameGui::submitTouch(x,y,2);
    }
    return g_originalPreDispatch ? g_originalPreDispatch(queue, ev) : 0;
}

int32_t hookedGetEvent(AInputQueue* queue, AInputEvent** outEvent) {
    const int32_t result = g_originalGetEvent ? g_originalGetEvent(queue, outEvent) : -1;
    if (result < 0 || !outEvent || !*outEvent) return result;

    AInputEvent* ev = *outEvent;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return result;

    const int action = AMotionEvent_getAction(ev);
    const int masked = action & AMOTION_EVENT_ACTION_MASK;
    const int pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    size_t idx = 0;
    if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
        masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        idx = static_cast<size_t>(pointerIndex);
    }
    if (idx >= AMotionEvent_getPointerCount(ev)) idx = 0;

    const float x = AMotionEvent_getX(ev, idx);
    const float y = AMotionEvent_getY(ev, idx);

    bool onGui = eclient_runtime::host::InGameGui::isTouchOnGui(x, y);
    bool wantCapture = false;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        wantCapture = io.WantCaptureMouse || ImGui::IsAnyItemActive() || io.MouseDown[0];
    }

    if (g_inputCount < 30) {
        char b[160]; snprintf(b,sizeof(b),"INPUT %d act=%d(%d) x=%.0f y=%.0f onGui=%d wantCap=%d ptrs=%zu", g_inputCount, masked, action, x, y, onGui?1:0, wantCapture?1:0, AMotionEvent_getPointerCount(ev));
        inputLog(b);
        g_inputCount++;
    }

    // Always give ImGui the pos so hover works even when not consumed
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

    // Consume only when actually on GUI or ImGui is dragging — otherwise game keeps its touches
    bool consume = onGui || wantCapture;
    if (!consume) return result;
    if (g_inputCount < 30) inputLog(consume ? "INPUT consume -> ImGui" : "INPUT pass -> game");
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
    __android_log_print(ANDROID_LOG_INFO, TAG, "Key hook installed (no hotkey, GUI persistent)");
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
    // also hook preDispatch as fallback (Minecraft GameActivity uses it)
    void* pre = dlsym(RTLD_DEFAULT, "AInputQueue_preDispatchEvent");
    if (pre) {
        if (DobbyHook(pre, (void*)hookedPreDispatch, (void**)&g_originalPreDispatch)==RS_SUCCESS) {
            inputLog("INPUT hook installed AInputQueue_preDispatchEvent OK");
        }
    }
    g_touchInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Touch routing for the GUI ready");
    inputLog("INPUT hook installed AInputQueue_getEvent OK");
    return true;
}
} // namespace

bool initialize() {
    const bool key = installKeyHook();
    const bool touch = installTouchHook();
    // GUI is persistent (minimized header always visible) — no hotkey needed.
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
