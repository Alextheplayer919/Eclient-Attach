#include "input/NativeInputHook.h"

#include <android/log.h>
#include <jni.h>
#include <atomic>

#include "gui/InGameGui.h"

namespace eclient_runtime::input {
namespace {
constexpr const char* TAG = "EClientInput";

// DESIGN DECISION (2026-09-24, per user requirement): NO hardware-key
// bindings. Volume Up conflicts with media volume on phones. The menu is
// toggled exclusively by the floating E-Client bar drawn by the GUI itself,
// so this TU installs no Dobby key hook at all. (The old
// nativeKeyHandler hook worked, but the binding was the problem, not the
// mechanism; it can be reinstated if a hardware bind is ever wanted.)
std::atomic_bool g_touchInstalled{false};
std::atomic<long> g_guiTouchCount{0};
// Gesture latch: a gesture that STARTS on the toggle bar must keep its events
// routed to ImGui until finger-up, even when MOVE positions fall outside the
// (moving) bar rect — otherwise dragging the bar would leak MOVE events into
// the game camera mid-drag.
std::atomic_bool g_barGestActive{false};
} // namespace

bool initialize() {
    // Touch arrives asynchronously via the view-layer listener
    // (tools/minecraft-touch-listener.smali) installed on each resumed
    // activity's decor view; it calls nativeTouchReady/nativeGuiTouch below.
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "Input: view-layer touch route armed; menu toggle = floating E-Client bar");
    return true;
}

void shutdown() {
    g_touchInstalled.store(false);
}

bool keyHookInstalled() { return false; }       // hardware keys not used
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

    // MotionEvent action-masked -> InGameGui touch codes (0=down, 1=move, 2=up)
    int mapped;
    switch (action) {
        case 0: /* ACTION_DOWN */          mapped = 0; break;
        case 2: /* ACTION_MOVE */          mapped = 1; break;
        case 1: /* ACTION_UP */
        case 3: /* ACTION_CANCEL */        mapped = 2; break;
        default: /* multi-touch variants */ mapped = 1; break;
    }

    if (!gui::menuOpen()) {
        // Menu closed: only gestures landing ON the floating toggle bar are
        // diverted to ImGui; everything else belongs to the game.
        if (mapped == 0) {
            if (!gui::hitTestToggleBar(x, y)) {
                eclient_runtime::input::g_barGestActive.store(false);
                return JNI_FALSE;
            }
            eclient_runtime::input::g_barGestActive.store(true);
        } else {
            if (!eclient_runtime::input::g_barGestActive.load()) {
                return JNI_FALSE;
            }
            if (mapped == 2) {
                eclient_runtime::input::g_barGestActive.store(false);
            }
        }
    } else {
        eclient_runtime::input::g_barGestActive.store(false);
    }

    gui::submitTouch(x, y, mapped);

    const long total = ++eclient_runtime::input::g_guiTouchCount;
    if (total == 1 || total % 100 == 0) {
        __android_log_print(ANDROID_LOG_INFO, "EClientInput",
            "GUI touch events flowing (%ld)", total);
    }
    return JNI_TRUE;  // consumed
}
