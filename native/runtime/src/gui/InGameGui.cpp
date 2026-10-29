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
#include "dobby.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

// ---------- E-Client shim + Zeff clickgui (full style, E-Client rebranded) ----------
#include "clickgui/eclient_shim.h"
#include "clickgui/RGB.h"
#include "clickgui/dark_theme.h"
#include "clickgui/variables.h"
#include "clickgui/menu_utils.h"
#include "clickgui/menu.h"
// hotkeys.h intentionally omitted — was uninvolved module (AKEYCODE_*) per user

#include "bridge/GameBridge.h"
#include "profile/PatchManager.h"

void EClient_Patches() {
    auto& pm = eclient_runtime::profile::PatchManager::instance();
    if (inMenu) {
        pm.set("NoHurtCam", false);
        pm.set("NoCamDistortion", false);
        pm.set("NoBoatRotation", false);
        pm.set("NoCamSleep", false);
        pm.set("PlaceCamera", false);
        pm.set("SlowDownTriggers", false);
        pm.set("XrayCameraThird", false);
        return;
    }
    pm.set("NoHurtCam", noHurtCam && !panic);
    pm.set("NoCamDistortion", noCamDist && !panic);
    pm.set("NoBoatRotation", noBoatRotation && !panic);
    pm.set("NoCamSleep", noCamSleep && !panic);
    pm.set("PlaceCamera", placeCam && !panic);
    pm.set("SlowDownTriggers", offSlowdownTrigger && !panic);
    pm.set("XrayCameraThird", xrayCam && !panic);
    pm.set("AntiKnockback", antiKnockback && !panic);
    pm.set("NoSlowDown", noSlowDown && !panic);
    pm.set("NoWaterDrown", offDrown && !panic);
    pm.set("NoLavaDrown", offDrown && !panic);
    pm.set("Noclip", noclip && !panic);
    pm.set("AutoClickMine", clickAutoMine && !panic);
    pm.set("NoEmoteCooldown", noEmoteCooldown && !panic);
    pm.set("NoBlur", noBlur && !panic);
    pm.set("FullBright", (fullBright || xrayDefault) && !panic);
    pm.set("NoCaveVignette", noCaveVignette && !panic);
}

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};

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

// HOTFIX: track last size, only scale once (was scaling every frame → exponential style explosion → black/crash)
int g_lastW = 0, g_lastH = 0;
bool g_styleScaled = false;
ImGuiStyle g_baseStyle;

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
        ImGui::StyleColorsClassic();
        embraceTheDarkness();
        g_baseStyle = ImGui::GetStyle();
        g_baseStyle.TouchExtraPadding = ImVec2(6,6);
        // HOTFIX: use #version 100 like original Zeff (was #version 300 es → shader fail on some GPUs → black)
        ImGui_ImplOpenGL3_Init("#version 100");
        __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client ClickGUI (Zeff style, hotfix)");
    }

    screenWidth = (int)width;
    screenHeight = (int)height;
    // HOTFIX: only recompute scale when size actually changes, and apply style scaling once
    if (width != g_lastW || height != g_lastH) {
        scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
        calcResX = calculateResolutionX(screenWidth, 2480);
        ImGuiStyle& st = ImGui::GetStyle();
        st = g_baseStyle;
        // Zeff did *3.5, we use *1.5 for safety (was *0.5 every frame → explosion)
        st.ScaleAllSizes(scaleGlobal * 1.5f);
        g_lastW = width; g_lastH = height;
        g_styleScaled = true;
        __android_log_print(ANDROID_LOG_INFO, TAG, "Resize %dx%d scale %.2f", width, height, scaleGlobal);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.FontGlobalScale = std::max(1.0f, scaleGlobal * 1.0f);

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

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    BeginDraw();
    RenderNotifications();

    ImGui::EndFrame();
    ImGui::Render();
    // HOTFIX: removed glClear() — was wiping Minecraft's scene → black screen
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    {
        std::lock_guard<std::mutex> lock(g_rectMutex);
        if (g_window) {
            g_lastWindowPos = g_window->Pos;
            g_lastWindowSize = g_window->Size;
        }
    }

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

void toggleMenu() {}
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
    __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client host ready (hotfix)");
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
