#include "gui/InGameGui.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cfloat>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include "dobby.h"
#include "bridge/DebugFiles.h"
#include "bridge/GameBridge.h"
#include "modules/ModuleManager.h"
#include "profile/PatchManager.h"
#include "input/NativeInputHook.h"

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};
std::atomic_bool g_menuOpen{false};

// Whole-frame guard: menus and in-world rendering may swap on different
// threads/contexts transiently (world load). If a frame races, skip drawing
// the GUI for that frame instead of corrupting ImGui state.
std::mutex g_frameMutex;

// EGL context the ImGui GL objects were created in. On a world join the game
// can recreate its render context; a stale shader/program set then renders
// NOTHING (the "menu fine on title screen, invisible in game" bug). Detect
// the switch and rebuild ImGui's GL state.
std::atomic<EGLContext> g_imguiContext{EGL_NO_CONTEXT};

// Queued touch events: input thread pushes, render thread drains in order.
struct TouchSample {
    float x{0};
    float y{0};
    int action{0}; // 0=down, 1=move, 2=up/cancel
};
std::mutex g_touchMutex;
std::vector<TouchSample> g_touchQueue;
float g_lastTouchX = -1.0f;
float g_lastTouchY = -1.0f;
bool g_lastTouchDown = false;
std::atomic<std::uint64_t> g_touchesSeen{0};

// Window->surface scaling, recomputed per frame (dynamic resolution).
std::atomic<float> g_scaleX{1.0f};
std::atomic<float> g_scaleY{1.0f};

// Floating toggle bar rect in DISPLAY (surface-pixel) coordinates.
// Written on the render thread, read by the input route for hit-testing.
std::mutex g_barMutex;
float g_barX = 24.0f;
float g_barY = 120.0f;
float g_barW = 0.0f;
float g_barH = 0.0f;
bool g_barSeenOnce = false;

// Stored by JNI_OnLoad; used to read the real display metrics from the
// platform thread-safely.
JavaVM* g_javaVm = nullptr;

// Reads android.content.res.Resources.getSystem().getDisplayMetrics() through
// JNI: the ACTUAL on-screen pixel size of the default display at the current
// rotation, independent of any EGL hook timing, and exactly the coordinate
// space MotionEvents are delivered in.
bool queryDisplayMetrics(int& outW, int& outH) {
    outW = 0;
    outH = 0;
    if (!g_javaVm) return false;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }

    bool ok = false;
    jclass resCls = env->FindClass("android/content/res/Resources");
    if (resCls && !env->ExceptionCheck()) {
        jmethodID getSystem = env->GetStaticMethodID(
            resCls, "getSystem", "()Landroid/content/res/Resources;");
        jobject res = getSystem ? env->CallStaticObjectMethod(resCls, getSystem) : nullptr;
        if (res && !env->ExceptionCheck()) {
            jmethodID getDm = env->GetMethodID(
                resCls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
            jobject dm = getDm ? env->CallObjectMethod(res, getDm) : nullptr;
            if (dm && !env->ExceptionCheck()) {
                jclass dmCls = env->GetObjectClass(dm);
                jfieldID wField = env->GetFieldID(dmCls, "widthPixels", "I");
                jfieldID hField = env->GetFieldID(dmCls, "heightPixels", "I");
                if (wField && hField) {
                    outW = env->GetIntField(dm, wField);
                    outH = env->GetIntField(dm, hField);
                    ok = outW > 0 && outH > 0;
                }
                env->DeleteLocalRef(dmCls);
                env->DeleteLocalRef(dm);
            }
            env->DeleteLocalRef(res);
        }
        env->DeleteLocalRef(resCls);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_javaVm->DetachCurrentThread();
    return ok;
}

void drawModuleList() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    const auto& modules = mgr.all();
    const bool patchReady =
        eclient_runtime::profile::PatchManager::instance().ready();

    // Group by category while preserving registration order.
    static const char* kOrder[] = {"Combat", "Movement", "Visual", "Player", "Misc"};
    std::unordered_map<std::string, std::vector<const eclient_runtime::modules::ModuleState*>> byCat;
    for (const auto& m : modules) {
        const std::string cat = m.category.empty() ? "Misc" : m.category;
        byCat[cat].push_back(&m);
    }

    if (ImGui::BeginTabBar("##cats", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (const char* cat : kOrder) {
            auto it = byCat.find(cat);
            if (it == byCat.end() || it->second.empty()) continue;
            if (!ImGui::BeginTabItem(cat)) continue;

            ImGui::BeginChild((std::string("##scroll_") + cat).c_str(),
                              ImVec2(0, 320), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto* module : it->second) {
                const bool inert = module->memoryBacked && !patchReady;
                bool enabled = module->enabled;
                // Larger hit target for touch.
                ImGui::PushID(module->name.c_str());
                if (inert) ImGui::BeginDisabled(true);
                if (ImGui::Checkbox(module->name.c_str(), &enabled)) {
                    if (!mgr.setEnabled(module->name, enabled)) {
                        enabled = module->enabled;
                        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "Toggle rejected by patch layer: %s", module->name.c_str());
                    }
                }
                if (inert) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (inert) {
                        ImGui::SetTooltip("%s\n[%s] patch profile not validated —\n"
                                          "this build's binary does not match the 1.21.111 reference bytes",
                                          module->description.c_str(),
                                          module->category.c_str());
                    } else {
                        ImGui::SetTooltip("%s\n[%s]%s",
                                          module->description.c_str(),
                                          module->category.c_str(),
                                          module->memoryBacked ? " memory patch" : " runtime hook");
                    }
                }

                // Melody-style per-module settings panel (touch: tap arrow to expand).
                if (!module->settings.empty()) {
                    ImGui::SameLine();
                    const bool open = ImGui::TreeNodeEx("##settings",
                                                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                        ImGuiTreeNodeFlags_Bullet |
                                                        ImGuiTreeNodeFlags_SpanAvailWidth);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%zu settings — tap to %s",
                                          module->settings.size(), open ? "collapse" : "expand");
                    }
                    if (open) {
                        ImGui::Indent(22.0f);
                        for (const auto& setting : module->settings) {
                            ImGui::PushID(setting.key.c_str());
                            switch (setting.type) {
                            case eclient_runtime::modules::Setting::Type::Bool: {
                                bool v = setting.boolValue;
                                if (ImGui::Checkbox(setting.label.c_str(), &v)) {
                                    mgr.setBoolSetting(module->name, setting.key, v);
                                }
                                break;
                            }
                            case eclient_runtime::modules::Setting::Type::Int: {
                                int v = setting.intValue;
                                if (ImGui::SliderInt(setting.label.c_str(), &v,
                                                     setting.intMin, setting.intMax)) {
                                    mgr.setIntSetting(module->name, setting.key, v);
                                }
                                break;
                            }
                            case eclient_runtime::modules::Setting::Type::Float: {
                                float v = setting.floatValue;
                                if (ImGui::SliderFloat(setting.label.c_str(), &v,
                                                       setting.floatMin, setting.floatMax,
                                                       "%.2f", ImGuiSliderFlags_None)) {
                                    mgr.setFloatSetting(module->name, setting.key, v);
                                }
                                break;
                            }
                            case eclient_runtime::modules::Setting::Type::Enum: {
                                const int current = setting.enumValue;
                                if (ImGui::BeginCombo(setting.label.c_str(),
                                    (current >= 0 && current < static_cast<int>(setting.options.size()))
                                        ? setting.options[static_cast<std::size_t>(current)].c_str()
                                        : "…")) {
                                    for (int oi = 0; oi < static_cast<int>(setting.options.size()); ++oi) {
                                        const bool sel = (oi == current);
                                        if (ImGui::Selectable(setting.options[static_cast<std::size_t>(oi)].c_str(), sel)) {
                                            mgr.setEnumSetting(module->name, setting.key, oi);
                                        }
                                        if (sel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                break;
                            }
                            }
                            if (!setting.tooltip.empty() &&
                                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                                     ImGuiHoveredFlags_DelayShort)) {
                                ImGui::SetTooltip("%s", setting.tooltip.c_str());
                            }
                            ImGui::PopID();
                        }
                        ImGui::Unindent(22.0f);
                        ImGui::Spacing();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// Small floating bar: the ONLY menu switch (no hardware keys). Draggable by
// its title bar; the button opens the full ClickGUI. Drawn only while the
// main menu is closed, so taps elsewhere always belong to the game.
void drawToggleBar() {
    static bool placed = false;
    if (!placed) {
        ImGui::SetNextWindowPos(ImVec2(24.0f, 120.0f), ImGuiCond_Always);
        placed = true;
    }
    ImGui::SetNextWindowSize(ImVec2(170.0f, 0.0f), ImGuiCond_Always);
    ImGui::Begin("E-Client##togglebar", nullptr,
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::Button("Open menu", ImVec2(150.0f, 52.0f))) {
        g_menuOpen.store(true);
    }
    ImGui::TextDisabled("drag this title to move");

    // Record the rect for the input-route hit test.
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    {
        std::lock_guard lock(g_barMutex);
        g_barX = pos.x;
        g_barY = pos.y;
        g_barW = size.x;
        g_barH = size.y;
        g_barSeenOnce = true;
    }
    ImGui::End();
}

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_originalSwap = nullptr;
std::atomic<int> g_physicalWidth{0};
std::atomic<int> g_physicalHeight{0};

void ensureImGuiInitialized() {
    const EGLContext current = eglGetCurrentContext();
    const EGLContext recorded = g_imguiContext.load();

    if (g_imguiReady.load() && recorded != EGL_NO_CONTEXT && current != recorded) {
        // Context was recreated by the game (e.g. world join): ImGui's
        // shader/program/FBO objects belong to the dead context. Rebuild.
        __android_log_print(ANDROID_LOG_INFO, TAG,
            "EGL context changed — rebuilding ImGui GL state");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady.store(false);
        g_imguiContext.store(EGL_NO_CONTEXT);
    }

    if (!g_imguiReady.load()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io0 = ImGui::GetIO();
        io0.IniFilename = nullptr;
        io0.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.TouchExtraPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(10.0f, 8.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        style.ScrollbarSize = 28.0f;
        style.WindowRounding = 8.0f;
        style.FrameRounding = 6.0f;
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_imguiContext.store(current);
        g_imguiReady.store(true);
        __android_log_print(ANDROID_LOG_INFO, TAG, "ImGui renderer initialized");
    }
}

void drainTouchQueue(ImGuiIO& io, float scaleX, float scaleY) {
    std::lock_guard lock(g_touchMutex);
    if (!g_touchQueue.empty()) {
        for (const auto& sample : g_touchQueue) {
            const float sx = sample.x * scaleX;
            const float sy = sample.y * scaleY;
            g_lastTouchX = sx;
            g_lastTouchY = sy;
            io.AddMousePosEvent(sx, sy);
            if (sample.action == 0) {
                g_lastTouchDown = true;
                io.AddMouseButtonEvent(0, true);
            } else if (sample.action == 2) {
                g_lastTouchDown = false;
                io.AddMouseButtonEvent(0, false);
            }
        }
        g_touchQueue.clear();
    } else if (g_lastTouchDown) {
        io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
        io.AddMouseButtonEvent(0, true);
    } else if (g_lastTouchX >= 0.0f) {
        io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
        io.AddMouseButtonEvent(0, false);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(0, false);
    }
}

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (g_ready.load()) {
        std::unique_lock<std::mutex> frameLock(g_frameMutex, std::try_to_lock);
        if (frameLock.owns_lock()) {
            // Backfill physical size via DisplayMetrics (the create-surface
            // hook installs too late to see the game's first surface).
            if (g_physicalWidth.load() <= 0 || g_physicalHeight.load() <= 0) {
                int dispW = 0;
                int dispH = 0;
                if (queryDisplayMetrics(dispW, dispH)) {
                    g_physicalWidth.store(dispW);
                    g_physicalHeight.store(dispH);
                }
            }

            EGLint width = 0;
            EGLint height = 0;
            eglQuerySurface(display, surface, EGL_WIDTH, &width);
            eglQuerySurface(display, surface, EGL_HEIGHT, &height);
            const int physW = g_physicalWidth.load();
            const int physH = g_physicalHeight.load();
            const bool needsScale = physW > 0 && physH > 0 && (physW != width || physH != height);
            const float scaleX = needsScale ? static_cast<float>(width) / static_cast<float>(physW) : 1.0f;
            const float scaleY = needsScale ? static_cast<float>(height) / static_cast<float>(physH) : 1.0f;
            g_scaleX.store(scaleX);
            g_scaleY.store(scaleY);

            if (width > 0 && height > 0) {
                ensureImGuiInitialized();
                if (g_imguiReady.load()) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
                    io.FontGlobalScale = std::max(1.5f, static_cast<float>(std::min(width, height)) / 360.0f);

                    drainTouchQueue(io, scaleX, scaleY);

                    ImGui_ImplOpenGL3_NewFrame();
                    ImGui::NewFrame();

                    const bool open = g_menuOpen.load();
                    if (!open) {
                        drawToggleBar();
                    } else {
                        const float winW = std::min(480.0f, static_cast<float>(width) * 0.92f);
                        const float winH = std::min(520.0f, static_cast<float>(height) * 0.85f);
                        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
                        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(width) * 0.04f,
                                                       static_cast<float>(height) * 0.06f),
                                                ImGuiCond_FirstUseEver);

                        bool keepOpen = open;
                        ImGui::Begin("E-Client 1.21.111", &keepOpen,
                                     ImGuiWindowFlags_NoCollapse);

                        const auto bridge = eclient_runtime::GameBridge::instance().snapshot();
                        ImGui::Text("Minecraft: %s", bridge.libraryLoaded ? "YES" : "NO");
                        const auto& patches = eclient_runtime::profile::PatchManager::instance();
                        ImGui::Text("Patch profile: %s", patches.ready() ? "VALIDATED" : "NOT VALIDATED");
                        if (!patches.ready()) {
                            ImGui::TextWrapped("%s", patches.status().c_str());
                        }
                        ImGui::Text("Touch: %s | taps: %llu",
                            eclient_runtime::input::touchHookInstalled()
                                ? "view listener ready"
                                : "waiting for window…",
                            static_cast<unsigned long long>(g_touchesSeen.load()));
                        ImGui::TextDisabled("Toggle: floating E-Client bar (no hardware keys)");
                        ImGui::Separator();

                        drawModuleList();

                        ImGui::Separator();
                        ImGui::TextDisabled("Patches fail closed if the 1.21.111 profile does not match.");
                        ImGui::End();
                        if (!keepOpen) g_menuOpen.store(false);
                    }

                    ImGui::Render();

                    // In-world the game may leave an OFFSCREEN framebuffer
                    // and/or a reduced viewport bound at swap time (bloom,
                    // water, shadow passes). ImGui draws into whatever is
                    // bound — on the title screen that is FBO 0, which is why
                    // the menu appeared there but NOT in a server/world.
                    // Force the default framebuffer + full-surface viewport
                    // for the GUI pass, then restore the game's state.
                    GLint lastFbo = 0;
                    GLint lastViewport[4] = {0, 0, 0, 0};
                    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &lastFbo);
                    glGetIntegerv(GL_VIEWPORT, lastViewport);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(0, 0, width, height);
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                    glBindFramebuffer(GL_FRAMEBUFFER, lastFbo);
                    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
                }
            }
        }
    }
    return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;
}

using CreateWindowSurfaceFn = EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
CreateWindowSurfaceFn g_originalCreateSurface = nullptr;

EGLSurface hookedCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                      EGLNativeWindowType window, const EGLint* attribs) {
    if (window) {
        auto* nativeWindow = reinterpret_cast<ANativeWindow*>(window);
        g_physicalWidth.store(ANativeWindow_getWidth(nativeWindow));
        g_physicalHeight.store(ANativeWindow_getHeight(nativeWindow));
    }
    return g_originalCreateSurface ? g_originalCreateSurface(display, config, window, attribs)
                                   : EGL_NO_SURFACE;
}

bool hookSwapBuffers() {
    if (g_originalSwap) return true;
    void* swapSymbol = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!swapSymbol) swapSymbol = reinterpret_cast<void*>(eglSwapBuffers);
    if (!swapSymbol) return false;
    if (DobbyHook(swapSymbol, reinterpret_cast<void*>(hookedSwap),
                  reinterpret_cast<void**>(&g_originalSwap)) != RS_SUCCESS) {
        return false;
    }

    void* createSymbol = dlsym(RTLD_DEFAULT, "eglCreateWindowSurface");
    if (!createSymbol) createSymbol = reinterpret_cast<void*>(eglCreateWindowSurface);
    if (createSymbol) {
        DobbyHook(createSymbol, reinterpret_cast<void*>(hookedCreateWindowSurface),
                  reinterpret_cast<void**>(&g_originalCreateSurface));
    }
    return g_originalSwap != nullptr;
}
} // namespace

void toggleMenu() { g_menuOpen.store(!g_menuOpen.load()); }

void setJavaVm(JavaVM* vm) {
    g_javaVm = vm;
    eclient_runtime::debugfiles::setJavaVm(vm); // feeds public-Download debug logs
}

std::uint64_t touchesSeen() { return g_touchesSeen.load(); }

bool hitTestToggleBar(float physicalX, float physicalY) {
    const float sx = physicalX * g_scaleX.load();
    const float sy = physicalY * g_scaleY.load();
    std::lock_guard lock(g_barMutex);
    if (!g_barSeenOnce) return false;
    return sx >= g_barX && sx <= g_barX + g_barW &&
           sy >= g_barY && sy <= g_barY + g_barH;
}

void setPhysicalWindowSize(int width, int height) {
    g_physicalWidth.store(width);
    g_physicalHeight.store(height);
}

void submitTouch(float x, float y, int action) {
    g_touchesSeen.fetch_add(1);
    std::lock_guard lock(g_touchMutex);
    // Cap queue so a stuck input thread cannot grow without bound.
    if (g_touchQueue.size() > 64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin() + 32);
    g_touchQueue.push_back(TouchSample{x, y, action});
}

bool menuOpen() { return g_menuOpen.load(); }

bool initialize() {
    std::lock_guard lock(g_mutex);
    if (g_ready.load()) return true;

    // Seed the physical size from the platform BEFORE render starts, so touch
    //→pixel scaling is correct even on devices with dynamic resolution where
    // the EGL render buffer is smaller than the window.
    int dispW = 0;
    int dispH = 0;
    if (queryDisplayMetrics(dispW, dispH)) {
        setPhysicalWindowSize(dispW, dispH);
        __android_log_print(ANDROID_LOG_INFO, TAG,
            "Physical display %dx%d via DisplayMetrics", dispW, dispH);
    } else {
        __android_log_print(ANDROID_LOG_WARN, TAG,
            "DisplayMetrics unavailable; touch scale may be off on dynamic-res devices");
    }

    if (!hookSwapBuffers()) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "eglSwapBuffers hook failed");
        return false;
    }
    g_ready.store(true);
    g_menuOpen.store(false);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Render host ready");
    return true;
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_ready.store(false);
    g_menuOpen.store(false);
    {
        std::lock_guard tlock(g_touchMutex);
        g_touchQueue.clear();
    }
    g_lastTouchX = -1.0f;
    g_lastTouchY = -1.0f;
    g_lastTouchDown = false;
    if (g_imguiReady.exchange(false)) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_imguiContext.store(EGL_NO_CONTEXT);
    }
}

} // namespace eclient_runtime::host::InGameGui
