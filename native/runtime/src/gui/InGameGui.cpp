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
#include "bridge/GameBridge.h"
#include "modules/ModuleManager.h"

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};
std::atomic_bool g_menuOpen{false};

// Queued touch events: input thread pushes, render thread drains in order.
// A single atomic pair was overwriting DOWN with UP between frames, so ImGui
// never saw a complete click.
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

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_originalSwap = nullptr;
std::atomic<int> g_physicalWidth{0};
std::atomic<int> g_physicalHeight{0};

void drawModuleList() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    const auto& modules = mgr.all();

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
                bool enabled = module->enabled;
                // Larger hit target for touch.
                ImGui::PushID(module->name.c_str());
                if (ImGui::Checkbox(module->name.c_str(), &enabled)) {
                    if (!mgr.setEnabled(module->name, enabled)) {
                        // Revert visual if patch layer rejected the change.
                        enabled = module->enabled;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n[%s]%s",
                                      module->description.c_str(),
                                      module->category.c_str(),
                                      module->memoryBacked ? " memory patch" : " runtime hook");
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (g_ready.load()) {
        EGLint width = 0;
        EGLint height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        const int physW = g_physicalWidth.load();
        const int physH = g_physicalHeight.load();
        const bool needsScale = physW > 0 && physH > 0 && (physW != width || physH != height);
        const float scaleX = needsScale ? static_cast<float>(width) / static_cast<float>(physW) : 1.0f;
        const float scaleY = needsScale ? static_cast<float>(height) / static_cast<float>(physH) : 1.0f;

        if (!g_imguiReady.exchange(true)) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io0 = ImGui::GetIO();
            io0.IniFilename = nullptr;
            io0.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            // Touch-friendly: windows can be moved from anywhere, larger frame padding.
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.TouchExtraPadding = ImVec2(8.0f, 8.0f);
            style.FramePadding = ImVec2(10.0f, 8.0f);
            style.ItemSpacing = ImVec2(10.0f, 8.0f);
            style.ScrollbarSize = 28.0f;
            style.WindowRounding = 8.0f;
            style.FrameRounding = 6.0f;
            ImGui_ImplOpenGL3_Init("#version 300 es");
            __android_log_print(ANDROID_LOG_INFO, TAG, "ImGui renderer initialized");
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.FontGlobalScale = std::max(1.5f, static_cast<float>(std::min(width, height)) / 360.0f);

        // Drain the touch queue in arrival order so DOWN/MOVE/UP sequences reach ImGui.
        {
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
                    // action == 1 (move): position only, button state unchanged
                }
                g_touchQueue.clear();
            } else if (g_lastTouchDown) {
                // Finger still down with no new events: keep position + button.
                io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                io.AddMouseButtonEvent(0, true);
            } else if (g_lastTouchX >= 0.0f) {
                // After release keep last hover for a frame so ImGui can finish click.
                io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                io.AddMouseButtonEvent(0, false);
            } else {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        const bool open = g_menuOpen.load();
        if (open) {
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
            ImGui::Text("Build: %s", bridge.buildId.empty() ? "unknown" : bridge.buildId.c_str());
            ImGui::TextDisabled("Volume Up = toggle menu | drag scrollbar to scroll");
            ImGui::Separator();

            drawModuleList();

            ImGui::Separator();
            ImGui::TextDisabled("Patches fail closed if the 1.21.111 profile does not match.");
            ImGui::End();
            if (!keepOpen) g_menuOpen.store(false);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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

void setPhysicalWindowSize(int width, int height) {
    g_physicalWidth.store(width);
    g_physicalHeight.store(height);
}

void submitTouch(float x, float y, int action) {
    std::lock_guard lock(g_touchMutex);
    // Cap queue so a stuck input thread cannot grow without bound.
    if (g_touchQueue.size() > 64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin() + 32);
    g_touchQueue.push_back(TouchSample{x, y, action});
}

bool menuOpen() { return g_menuOpen.load(); }

bool initialize() {
    std::lock_guard lock(g_mutex);
    if (g_ready.load()) return true;
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
    }
}

} // namespace eclient_runtime::host::InGameGui
