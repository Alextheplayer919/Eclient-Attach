#include "gui/InGameGui.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cfloat>
#include <mutex>
#include <atomic>
#include <vector>

// ---------- E-Client shim + Zeff clickgui (full style, E-Client rebranded) ----------
// User requested: revert to Zeff's clickgui style (8 pages, full tabs) but keep E-Client title
// and no Zeff/Discord links, and adapt client to it (variables.h as source of truth).
// We include the shim first, then the Zeff files in original order. Only this TU includes
// variables.h/menu.h, so globals are defined once. GameplayModules reads them via extern.
#include "clickgui/eclient_shim.h"

// Zeff's RGB helper (defines redd/greenn/bluee and performRGBChange)
#include "clickgui/RGB.h"
#include "clickgui/dark_theme.h"

// nlohmann json stub is already in eclient_shim.h, so variables.h can use `json cfg;`
#include "clickgui/variables.h"
#include "clickgui/menu_utils.h"
#include "clickgui/menu.h"
#include "clickgui/hotkeys.h"

#include "bridge/GameBridge.h"
#include "profile/PatchManager.h"

// Implement Patches() bridging: Zeff's menu calls Patches() every frame, we forward to PatchManager
void EClient_Patches() {
    auto& pm = eclient_runtime::profile::PatchManager::instance();
    // In-menu: Zeff restores some patches while menu open — we keep same logic but via PatchManager
    // For E-Client we treat all patch-backed modules as fail-closed: only enable if PatchManager has spec
    // and the corresponding bool is true && !panic
    if (inMenu) {
        // Zeff restores camera patches while inMenu — we do same to avoid visual glitch when menu open
        pm.set("NoHurtCam", false);
        pm.set("NoCamDistortion", false);
        pm.set("NoBoatRotation", false);
        pm.set("NoCamSleep", false);
        pm.set("PlaceCamera", false);
        pm.set("SlowDownTriggers", false);
        pm.set("XrayCameraThird", false);
        return;
    }
    // Normal patch application — map Zeff bools -> E-Client patch names
    pm.set("NoHurtCam", noHurtCam && !panic);
    pm.set("NoCamDistortion", noCamDist && !panic);
    pm.set("NoBoatRotation", noBoatRotation && !panic);
    pm.set("NoCamSleep", noCamSleep && !panic);
    pm.set("PlaceCamera", placeCam && !panic);
    pm.set("SlowDownTriggers", offSlowdownTrigger && !panic);
    pm.set("XrayCameraThird", xrayCam && !panic);
    pm.set("AntiKnockback", antiKnockback && !panic);
    pm.set("NoSlowDown", noSlowDown && !panic);
    // offDrown maps to both water/lava (Zeff has single offDrown, we have two)
    pm.set("NoWaterDrown", offDrown && !panic);
    pm.set("NoLavaDrown", offDrown && !panic);
    pm.set("Noclip", noclip && !panic);
    pm.set("AutoClickMine", clickAutoMine && !panic);
    pm.set("NoEmoteCooldown", noEmoteCooldown && !panic);
    pm.set("NoBlur", noBlur && !panic);
    pm.set("FullBright", (fullBright || xrayDefault) && !panic);
    pm.set("NoCaveVignette", noCaveVignette && !panic);
    // Font patches are not applicable to E-Client (leave as restore)
}

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};

// Touch handling (same as before, but now driven by Zeff's expected inMenu/inHudScreen flags)
struct TouchSample { float x{0}, y{0}; int action{0}; };
std::mutex g_touchMutex;
std::vector<TouchSample> g_touchQueue;
float g_lastTouchX = -1.0f, g_lastTouchY = -1.0f;
bool g_lastTouchDown = false;

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_originalSwap = nullptr;
std::atomic<int> g_physicalWidth{0}, g_physicalHeight{0};
std::mutex g_rectMutex;
ImVec2 g_lastWindowPos{0,0}, g_lastWindowSize{0,0};

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (!g_ready.load()) return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;

    EGLint width=0, height=0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    const int physW = g_physicalWidth.load(), physH = g_physicalHeight.load();
    const bool needsScale = physW>0 && physH>0 && (physW!=width || physH!=height);
    const float scaleX = needsScale ? (float)width / physW : 1.0f;
    const float scaleY = needsScale ? (float)height / physH : 1.0f;

    if (!g_imguiReady.exchange(true)) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io0 = ImGui::GetIO();
        io0.IniFilename = nullptr;
        // Zeff's init: Classic style + embraceTheDarkness()
        ImGui::StyleColorsClassic();
        embraceTheDarkness();
        ImGuiStyle& st = ImGui::GetStyle();
        st.TouchExtraPadding = ImVec2(6,6);
        // Zeff used #version 100, we use 300 es for E-Client GLES3
        ImGui_ImplOpenGL3_Init("#version 300 es");
        __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client ClickGUI (Zeff style, E-Client rebranded) initialized");
        // Zeff's variables: readMe controls initial Description popup — we keep it (user must click I READ)
        // For E-Client we could set readMe=true to skip, but user wanted Zeff style, so keep popup
    }

    // Update screen globals that Zeff's menu.h/menu_utils.h rely on (originally set in main.cpp's swap hook)
    screenWidth = (int)width;
    screenHeight = (int)height;
    scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
    calcResX = calculateResolutionX(screenWidth, 2480);
    // Apply style scaling (Zeff did: style.ScaleAllSizes(scaleGlobal*3.5); io.FontGlobalScale=scaleGlobal)
    ImGui::GetStyle().ScaleAllSizes(scaleGlobal * 0.5f); // mild scaling, avoid over-scale every frame
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.FontGlobalScale = std::max(1.0f, scaleGlobal * 1.2f);

    // Input: feed touch queue to ImGui (Zeff used direct touch, we use queue from NativeInputHook)
    {
        std::lock_guard<std::mutex> lock(g_touchMutex);
        if (!g_touchQueue.empty()) {
            for (auto &s: g_touchQueue) {
                float sx = s.x*scaleX, sy = s.y*scaleY;
                g_lastTouchX = sx; g_lastTouchY = sy;
                io.AddMousePosEvent(sx, sy);
                if (s.action==0) { g_lastTouchDown=true; io.AddMouseButtonEvent(0,true); }
                else if (s.action==2) { g_lastTouchDown=false; io.AddMouseButtonEvent(0,false); }
            }
            g_touchQueue.clear();
        } else if (g_lastTouchDown) {
            io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
            io.AddMouseButtonEvent(0,true);
        } else if (g_lastTouchX>=0) {
            io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
            io.AddMouseButtonEvent(0,false);
        } else {
            io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);
            io.AddMouseButtonEvent(0,false);
        }
    }

    // Zeff's per-frame: ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame(); BeginDraw(); RenderNotifications(); EndFrame etc.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Zeff's menu manages its own window (BeginDraw creates "Description" or "E-Client | Minecraft 1.21.111")
    // We just call it — it handles inMenu/panic logic internally
    BeginDraw();
    RenderNotifications();

    ImGui::EndFrame();
    ImGui::Render();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update rect for hit-test (use g_window if available, else approximate)
    {
        std::lock_guard<std::mutex> lock(g_rectMutex);
        if (g_window) {
            g_lastWindowPos = g_window->Pos;
            g_lastWindowSize = g_window->Size;
        } else {
            // fallback: use InGameGui's own tracking of E-Client window if g_window not set yet
            // Zeff's menu sets g_window = ImGui::GetCurrentWindow() inside BeginDraw
        }
    }

    // Zeff's menu also handles tick logic via TickFunctions() and performRGBChange() at end of BeginDraw
    // Those are already called inside BeginDraw() in menu.h (see end of BeginDraw)

    return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;
}

using CreateWindowSurfaceFn = EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
CreateWindowSurfaceFn g_originalCreateSurface = nullptr;
EGLSurface hookedCreateWindowSurface(EGLDisplay display, EGLConfig config, EGLNativeWindowType window, const EGLint* attribs) {
    if (window) {
        auto* nw = reinterpret_cast<ANativeWindow*>(window);
        g_physicalWidth.store(ANativeWindow_getWidth(nw));
        g_physicalHeight.store(ANativeWindow_getHeight(nw));
    }
    return g_originalCreateSurface ? g_originalCreateSurface(display, config, window, attribs) : EGL_NO_SURFACE;
}
bool hookSwapBuffers() {
    if (g_originalSwap) return true;
    void* sw = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!sw) sw = reinterpret_cast<void*>(eglSwapBuffers);
    if (!sw) return false;
    if (DobbyHook(sw, reinterpret_cast<void*>(hookedSwap), reinterpret_cast<void**>(&g_originalSwap)) != RS_SUCCESS) return false;
    void* cs = dlsym(RTLD_DEFAULT, "eglCreateWindowSurface");
    if (!cs) cs = reinterpret_cast<void*>(eglCreateWindowSurface);
    if (cs) DobbyHook(cs, reinterpret_cast<void*>(hookedCreateWindowSurface), reinterpret_cast<void**>(&g_originalCreateSurface));
    return g_originalSwap != nullptr;
}
} // namespace

void toggleMenu() { /* Zeff's menu is always present after readMe, not volume-key toggled */ }
void toggleCollapsed() {}
void setCollapsed(bool) {}
bool isCollapsed() { return false; }
bool isTouchOnGui(float x, float y) {
    std::lock_guard<std::mutex> l(g_rectMutex);
    if (g_lastWindowSize.x<=0) return false;
    return x>=g_lastWindowPos.x && x<=g_lastWindowPos.x+g_lastWindowSize.x &&
           y>=g_lastWindowPos.y && y<=g_lastWindowPos.y+g_lastWindowSize.y;
}
bool menuOpen() { return true; }
void setPhysicalWindowSize(int w,int h){ g_physicalWidth.store(w); g_physicalHeight.store(h); }
void submitTouch(float x,float y,int action){
    std::lock_guard<std::mutex> l(g_touchMutex);
    if (g_touchQueue.size()>64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin()+32);
    g_touchQueue.push_back({x,y,action});
}
bool initialize() {
    std::lock_guard<std::mutex> l(g_mutex);
    if (g_ready.load()) return true;
    if (!hookSwapBuffers()) { __android_log_print(ANDROID_LOG_ERROR, TAG, "eglSwapBuffers hook failed"); return false; }
    g_ready.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client host ready (Zeff clickgui style, E-Client rebranded, variables.h as source of truth)");
    return true;
}
void shutdown(){
    std::lock_guard<std::mutex> l(g_mutex);
    g_ready.store(false);
    { std::lock_guard<std::mutex> tl(g_touchMutex); g_touchQueue.clear(); }
    g_lastTouchX=-1; g_lastTouchY=-1; g_lastTouchDown=false;
    if (g_imguiReady.exchange(false)){ ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); }
}
} // namespace eclient_runtime::host::InGameGui
