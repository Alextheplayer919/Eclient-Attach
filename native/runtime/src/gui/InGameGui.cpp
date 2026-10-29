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
#include "modules/GameplayModules.h"

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};
std::atomic_bool g_collapsed{true}; // start minimized — only name panel visible

struct TouchSample {
    float x{0};
    float y{0};
    int action{0};
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

// Last frame window rect for hit-testing (in display pixels)
std::mutex g_rectMutex;
ImVec2 g_lastWindowPos{0, 0};
ImVec2 g_lastWindowSize{0, 0};

void drawModuleList() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    const auto& modules = mgr.all();
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
            // Fixed height child with scrollbar — touch drag works via mouse drag
            ImGui::BeginChild((std::string("##scroll_") + cat).c_str(),
                              ImVec2(0, 360), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto* module : it->second) {
                bool enabled = module->enabled;
                ImGui::PushID(module->name.c_str());
                if (ImGui::Checkbox(module->name.c_str(), &enabled)) {
                    if (!mgr.setEnabled(module->name, enabled)) {
                        enabled = module->enabled;
                    }
                }
                // ── FlyX sliders (Apollon-style factors 0.1-2.0) ──────────────────
                if (module->name == "FlyX" && enabled) {
                    float h = eclient_runtime::modules::gameplay::getFlyXUpH();
                    float v = eclient_runtime::modules::gameplay::getFlyXUpV();
                    ImGui::Indent(12);
                    if (ImGui::SliderFloat("Up H Factor##FlyX", &h, 0.1f, 2.0f, "%.2fx")) eclient_runtime::modules::gameplay::setFlyXUpH(h);
                    if (ImGui::SliderFloat("Up V Factor##FlyX", &v, 0.1f, 2.0f, "%.2fx")) eclient_runtime::modules::gameplay::setFlyXUpV(v);
                    ImGui::Unindent(12);
                }
                // ── KillAura full settings (Apollon v4.62) ──────────────────────
                if (module->name == "KillAura" && enabled) {
                    ImGui::Indent(12);
                    float kr = eclient_runtime::modules::gameplay::getKillRange();
                    float rr = eclient_runtime::modules::gameplay::getRotationsRange();
                    if (ImGui::SliderFloat("Kill Range##KA", &kr, 1.0f, 10.0f, "%.1f")) eclient_runtime::modules::gameplay::setKillRange(kr);
                    if (ImGui::SliderFloat("Rotations Range##KA", &rr, 1.0f, 15.0f, "%.1f")) eclient_runtime::modules::gameplay::setRotationsRange(rr);
                    int at = eclient_runtime::modules::gameplay::getAuraType();
                    const char* auraNames[] = {"Single","Nearest","Multi"};
                    if (ImGui::Combo("Aura Type##KA", &at, auraNames, 3)) eclient_runtime::modules::gameplay::setAuraType(at);
                    int kd = eclient_runtime::modules::gameplay::getMaxKillTick();
                    if (ImGui::SliderInt("Kill Delay##KA", &kd, 1, 20)) eclient_runtime::modules::gameplay::setMaxKillTick(kd);
                    bool sw = eclient_runtime::modules::gameplay::getSwing();
                    if (ImGui::Checkbox("Swing##KA", &sw)) eclient_runtime::modules::gameplay::setSwing(sw);
                    bool ro = eclient_runtime::modules::gameplay::getRotations();
                    if (ImGui::Checkbox("Rotations##KA", &ro)) eclient_runtime::modules::gameplay::setRotations(ro);
                    ImGui::SameLine();
                    bool hc = eclient_runtime::modules::gameplay::getHurtCheck();
                    if (ImGui::Checkbox("HurtCheck##KA", &hc)) eclient_runtime::modules::gameplay::setHurtCheck(hc);
                    ImGui::TextDisabled("TargetLock is separate module (Combat)");
                    ImGui::Unindent(12);
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // Persistent window — always present, collapsible to title bar
        const bool collapsed = g_collapsed.load();
        const float winW = collapsed ? 260.0f : std::min(480.0f, static_cast<float>(width) * 0.92f);
        const float winH = collapsed ? 42.0f : std::min(520.0f, static_cast<float>(height) * 0.85f);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(width) * 0.04f,
                                       static_cast<float>(height) * 0.06f),
                                ImGuiCond_FirstUseEver);

        // NoCollapse false so title bar is interactive; we handle collapse ourselves
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (collapsed) flags |= ImGuiWindowFlags_NoResize;

        // Use a single window that we manually collapse: header is always visible
        bool windowOpen = true;
        if (ImGui::Begin("E-Client 1.21.111", &windowOpen, flags)) {
            {
                std::lock_guard lock(g_rectMutex);
                g_lastWindowPos = ImGui::GetWindowPos();
                g_lastWindowSize = ImGui::GetWindowSize();
            }

            // Header row: title + minimize/maximize button
            // Make the whole title bar draggable and clickable to toggle
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();

            // Invisible button covering title bar for toggle (height ~ 32)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            std::string headerLabel = collapsed ? "E-Client 1.21.111  [+]##hdr" : "E-Client 1.21.111  [-]##hdr";
            // Header button spans full width minus close
            if (ImGui::Button(headerLabel.c_str(), ImVec2(-1, 0))) {
                g_collapsed.store(!collapsed);
            }
            ImGui::PopStyleColor(3);

            if (!collapsed) {
                const auto bridge = eclient_runtime::GameBridge::instance().snapshot();
                ImGui::Text("Minecraft: %s", bridge.libraryLoaded ? "YES" : "NO");
                ImGui::Text("Build: %s", bridge.buildId.empty() ? "unknown" : bridge.buildId.c_str());
                ImGui::TextDisabled("Tap header to minimize | drag scrollbar to scroll");
                ImGui::Separator();
                drawModuleList();
                ImGui::Separator();
                ImGui::TextDisabled("Patches fail closed if 1.21.111 profile mismatches.");
            }
        }
        ImGui::End();

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

void toggleMenu() { g_collapsed.store(!g_collapsed.load()); }
void toggleCollapsed() { g_collapsed.store(!g_collapsed.load()); }
void setCollapsed(bool c) { g_collapsed.store(c); }
bool isCollapsed() { return g_collapsed.load(); }
bool isTouchOnGui(float x, float y) {
    std::lock_guard lock(g_rectMutex);
    if (g_lastWindowSize.x <= 0 || g_lastWindowSize.y <= 0) return false;
    return x >= g_lastWindowPos.x && x <= g_lastWindowPos.x + g_lastWindowSize.x &&
           y >= g_lastWindowPos.y && y <= g_lastWindowPos.y + g_lastWindowSize.y;
}
bool menuOpen() { return !g_collapsed.load(); }

void setPhysicalWindowSize(int width, int height) {
    g_physicalWidth.store(width);
    g_physicalHeight.store(height);
}

void submitTouch(float x, float y, int action) {
    std::lock_guard lock(g_touchMutex);
    if (g_touchQueue.size() > 64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin() + 32);
    g_touchQueue.push_back(TouchSample{x, y, action});
}

bool initialize() {
    std::lock_guard lock(g_mutex);
    if (g_ready.load()) return true;
    if (!hookSwapBuffers()) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "eglSwapBuffers hook failed");
        return false;
    }
    g_ready.store(true);
    g_collapsed.store(true); // start minimized as requested
    __android_log_print(ANDROID_LOG_INFO, TAG, "Render host ready (persistent, minimized)");
    return true;
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_ready.store(false);
    g_collapsed.store(true);
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
