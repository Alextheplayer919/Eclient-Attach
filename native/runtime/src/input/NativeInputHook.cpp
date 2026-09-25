#include "input/NativeInputHook.h"

#include <atomic>

#include <jni.h>

#include "gui/InGameGui.h"

namespace eclient_runtime::input {
namespace {
std::atomic<bool> g_initialized{false};
// Toggle-bar gesture latch: while a touch gesture started on the bar is still
// held down, keep consuming its moves (the bar drags; the game only sees
// motion when the user swipes outside the bar). Per-pointer: pid 0 only.
std::atomic<bool> g_barGesture{false};
} // namespace

bool initialize() {
    g_initialized = true;
    return true;
}

void shutdown() {
    g_initialized = false;
    g_barGesture = false;
}

bool keyHookInstalled() { return g_initialized.load(); }
bool touchHookInstalled() { return g_initialized.load(); }

namespace {
// Map Android ACTION_* / ACTION_POINTER_* to our 0=down / 1=move / 2=up.
// 5=POINTER_DOWN, 6=POINTER_UP, 3=CANCEL are per-pointer; MOVE(2) arrives
// once per pointer from the Java listener; keyboard buttons use 4 (ignore).
int mapAction(int androidAction) {
    switch (androidAction) {
        case 0: case 5: return 0;
        case 2: return 1;
        case 1: case 3: case 6: return 2;
        default: return -1;
    }
}
} // namespace

// JNI contract with the Java listener: (int pointerId, int action, float x,
// float y) -> consumed:boolean — declared in minecraft-host-loader.smali as
// EClientLoaderProvider.nativeGuiTouch.
extern "C" JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_host_EClientLoaderProvider_nativeGuiTouch(
    JNIEnv* /*env*/, jclass /*clazz*/, jint pointerId, jint androidAction, jfloat x, jfloat y) {
    if (!g_initialized.load()) return JNI_FALSE;
    const int action = mapAction(androidAction);
    if (action < 0) return JNI_FALSE;

    // Menu open: every pointer drives ONLY ImGui (single mouse = pid 0).
    if (host::InGameGui::menuOpen()) {
        if (pointerId == 0) {
            g_barGesture = false;
            host::InGameGui::submitTouch(x, y, action);
        }
        return JNI_TRUE;
    }

    // Menu closed. Fly buttons first: any pointer claimed by a fly button is
    // consumed here so it never spins the camera or touches the world.
    if (host::InGameGui::flyTouch(pointerId, action, x, y)) {
        if (pointerId == 0) g_barGesture = false;
        return JNI_TRUE;
    }

    // Then the toggle bar / its move-gesture latch (primary pointer only).
    if (pointerId == 0) {
        if (action == 0) {
            g_barGesture = host::InGameGui::hitTestToggleBar(x, y);
        }
        if (g_barGesture.load()) {
            host::InGameGui::submitTouch(x, y, action);
            if (action == 2) g_barGesture = false;
            return JNI_TRUE;
        }
    }

    // Not ours: game handles the touch normally.
    return JNI_FALSE;
}

// Called by the host loader once the native library is loaded: marks the
// input route ready. Idempotent (also driven from MinecraftHost startup).
extern "C" JNIEXPORT void JNICALL Java_com_rubidiumclient_host_EClientLoaderProvider_nativeTouchReady(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    initialize();
}

} // namespace eclient_runtime::input
