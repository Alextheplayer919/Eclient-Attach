#pragma once

#include <cstdint>
#include <jni.h>

namespace eclient_runtime::host::InGameGui {

bool initialize();
void shutdown();
void toggleMenu();
bool menuOpen();

// Called once from JNI_OnLoad so the GUI can query the real display size
// through the platform (DisplayMetrics) even though the eglCreateWindowSurface
// hook usually installs too late to see the surface Minecraft already created.
// Stored only; the GUI never calls back into Java outside its own init thread.
void setJavaVm(JavaVM* vm);

// Total touch samples accepted by submitTouch() since process start.
// Diagnostic surfaced in the menu so a dead input path is visible on-device.
std::uint64_t touchesSeen();

// The true on-screen pixel size, captured from the window Minecraft actually
// creates its EGL surface on. This can differ from the surface's render-buffer
// size when the game uses dynamic resolution scaling, which is what touch
// coordinates are reported against.
void setPhysicalWindowSize(int width, int height);

// Thread-safe: may be called from Minecraft's input thread. The values are
// applied to ImGui on the render thread during the next frame.
// action: 0 = down, 1 = move, 2 = up/cancel.
void submitTouch(float x, float y, int action);

// Hit-test (PHYSICAL/window pixels, same space MotionEvents arrive in)
// against the floating toggle bar. Used by the input route so that when the
// main menu is closed, only taps landing ON the bar are diverted to ImGui —
// everything else keeps flowing into the game.
bool hitTestToggleBar(float physicalX, float physicalY);

// Fly module on-screen buttons: route a raw pointer event (PHYSICAL pixels,
// action 0=down/1=move/2=up) through the fly-button gesture tracker. Returns
// true when the event belongs to a fly button and must be consumed before it
// reaches the game. Inactive (fly off / buttons hidden / menu open) ⇒ false.
bool flyTouch(int pointerId, int action, float physicalX, float physicalY);

} // namespace eclient_runtime::host::InGameGui
