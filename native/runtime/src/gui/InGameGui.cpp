#include "gui/InGameGui.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cfloat>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <vector>
#include <fstream>
#include <signal.h>
#include <unistd.h>
#include "dobby.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include "clickgui/eclient_shim.h"
#include "clickgui/RGB.h"
#include "clickgui/dark_theme.h"
#include "clickgui/variables.h"
#include "clickgui/menu_utils.h"
#include "clickgui/menu.h"

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
std::atomic_bool g_crashed{false};
int g_frameCount = 0;

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
int g_lastW = 0, g_lastH = 0;
ImGuiStyle g_baseStyle;

void logFile(const char* msg) {
    __android_log_print(ANDROID_LOG_ERROR, TAG, "%s", msg);
    // also echo to stderr so tombstone may capture
    fprintf(stderr, "[EClient] %s\n", msg); fflush(stderr);
    FILE* f = fopen("/sdcard/Download/EClient_debug.log", "a");
    if (!f) f = fopen("/data/data/com.mojang.minecraftpe/cache/EClient_debug.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
    // also try /data/data/com.mojang.minecraftpe/files/ which is always writable
    FILE* f2 = fopen("/data/data/com.mojang.minecraftpe/files/EClient_debug.log", "a");
    if (f2) { fprintf(f2, "%s\n", msg); fflush(f2); fclose(f2); }
}
void crashHandler(int sig) {
    char b[128]; snprintf(b,sizeof(b), "CRASH signal %d frame %d", sig, g_frameCount);
    logFile(b);
    FILE* f = fopen("/sdcard/Download/EClient_crash.log", "w");
    if (!f) f = fopen("/data/data/com.mojang.minecraftpe/cache/EClient_crash.log", "w");
    if (f) { fprintf(f, "CRASH %d frame %d\n", sig, g_frameCount); fclose(f); }
    g_crashed = true;
}

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (!g_ready.load()) return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;
    if (g_crashed.load()) return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;

    // super defensive: if anything throws/crashes, we disable ourselves and just pass through
    // so you don't get stuck in black screen loop
    EGLint width=0, height=0;
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) || !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;
    }
    const int physW = g_physicalWidth.load(), physH = g_physicalHeight.load();
    const float scaleX = (physW>0 && physH>0 && physW!=width) ? (float)width / physW : 1.0f;
    const float scaleY = (physW>0 && physH>0 && physH!=height) ? (float)height / physH : 1.0f;

    if (!g_imguiReady.exchange(true)) {
        logFile("IMGUI: creating context");
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io0 = ImGui::GetIO();
        io0.IniFilename = nullptr;
        ImGui::StyleColorsClassic();
        embraceTheDarkness();
        g_baseStyle = ImGui::GetStyle();
        g_baseStyle.TouchExtraPadding = ImVec2(6,6);
        bool ok = ImGui_ImplOpenGL3_Init("#version 100");
        logFile(ok ? "IMGUI: Init #version 100 OK" : "IMGUI: Init FAILED");
        if (!ok) { g_crashed = true; return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE; }
        // skip Description popup (was crashing at FRAME 0 BeginDraw) — go straight to main menu
        readMe = true;
        logFile("IMGUI: ready + readMe=true");
    }

    screenWidth = (int)width;
    screenHeight = (int)height;
    if (width != g_lastW || height != g_lastH) {
        scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
        calcResX = calculateResolutionX(screenWidth, 2480);
        ImGuiStyle& st = ImGui::GetStyle();
        st = g_baseStyle;
        st.ScaleAllSizes(scaleGlobal * 1.5f);
        g_lastW = width; g_lastH = height;
        char b[128]; snprintf(b,sizeof(b),"RESIZE %dx%d scale %.2f",width,height,scaleGlobal);
        logFile(b);
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

    if (g_frameCount < 5) { char b[64]; snprintf(b,sizeof(b),"FRAME %d BeginDraw", g_frameCount); logFile(b); }
    // guard: need valid GL context for ImGui, else it will crash (splash uses different context)
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (!ver) { logFile("FRAME: no GL_VERSION, skip"); return g_originalSwap ? g_originalSwap(display,surface) : EGL_FALSE; }
    if (!ImGui::GetCurrentContext()) { logFile("FRAME: no ImGui context, skip"); return g_originalSwap ? g_originalSwap(display,surface) : EGL_FALSE; }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    // diagnostic: try minimal draw first to prove ImGui/GL works, then full menu
    if (g_frameCount == 0) {
        logFile("FRAME 0 minimal Begin test");
        if (ImGui::Begin("E-Client Test", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::Text("E-Client minimal OK — scale %.2f", scaleGlobal);
            ImGui::SetWindowSize(ImVec2(600,300));
            ImGui::End();
        }
        logFile("FRAME 0 minimal End OK");
    } else {
        BeginDraw();
        RenderNotifications();
    }
    ImGui::EndFrame();
    ImGui::Render();
    // RenderDrawData does GL — guard it
    if (g_frameCount < 5) logFile("FRAME before RenderDrawData");
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (g_frameCount < 5) {
        char b[64]; snprintf(b,sizeof(b),"FRAME %d done", g_frameCount);
        logFile(b);
        // dump which path we took
        if (g_frameCount==0) logFile("FRAME 0 minimal path done — full menu not yet tested");
    }
    g_frameCount++;

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
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    void* sw = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!sw) sw = reinterpret_cast<void*>(eglSwapBuffers);
    if (!sw) { logFile("HOOK: eglSwapBuffers not found"); return false; }
    if (DobbyHook(sw, reinterpret_cast<void*>(hookedSwap), reinterpret_cast<void**>(&g_originalSwap)) != RS_SUCCESS) {
        logFile("HOOK: DobbyHook failed"); return false;
    }
    void* cs = dlsym(RTLD_DEFAULT, "eglCreateWindowSurface");
    if (!cs) cs = reinterpret_cast<void*>(eglCreateWindowSurface);
    if (cs) DobbyHook(cs, reinterpret_cast<void*>(hookedCreateWindowSurface), reinterpret_cast<void**>(&g_originalCreateSurface));
    logFile("HOOK: eglSwapBuffers OK + crash handler");
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
    // clear old log
    remove("/sdcard/Download/EClient_debug.log");
    logFile("INIT: E-Client host starting");
    if (g_ready.load()) return true;
    if (!hookSwapBuffers()) { logFile("INIT: hook failed"); return false; }
    g_ready.store(true);
    logFile("INIT: host ready — if you see black, check EClient_debug.log in Download");
    return true;
}
void shutdown(){
    std::lock_guard<std::mutex> l(g_mutex);
    g_ready.store(false);
    { std::lock_guard<std::mutex> tl(g_touchMutex); g_touchQueue.clear(); }
    g_lastTouchX=-1; g_lastTouchY=-1; g_lastTouchDown=false;
    if (g_imguiReady.exchange(false)){ ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); logFile("SHUTDOWN"); }
}
} // namespace eclient_runtime::host::InGameGui
