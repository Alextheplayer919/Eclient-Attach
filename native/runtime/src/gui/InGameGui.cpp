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
        // BIGGER gui: was 1.5f → 1.95f so text/categories breathe
        st.ScaleAllSizes(scaleGlobal * 1.95f);
        // more grab area for drag on phone
        st.TouchExtraPadding = ImVec2(10,10);
        g_lastW = width; g_lastH = height;
        char b[128]; snprintf(b,sizeof(b),"RESIZE %dx%d scale %.2f",width,height,scaleGlobal);
        logFile(b);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    // BIGGER text: was max(1, scale) → scale*1.35 so categories not cramped
    io.FontGlobalScale = std::max(1.20f, scaleGlobal * 1.35f);

    {
        std::lock_guard<std::mutex> lock(g_touchMutex);
        if (!g_touchQueue.empty()) {
            for (auto &s: g_touchQueue) {
                float sx = s.x*scaleX, sy = s.y*scaleY;
                g_lastTouchX = sx; g_lastTouchY = sy;
                io.AddMousePosEvent(sx, sy);
                // 0=down, 1=move, 2=up/cancel — was only 0/2, so drag never fired
                if (s.action==0) { g_lastTouchDown=true; io.AddMouseButtonEvent(0,true); }
                else if (s.action==2 || s.action==3) { g_lastTouchDown=false; io.AddMouseButtonEvent(0,false); }
                else if (s.action==1) { /* move while held — keep button */ io.AddMouseButtonEvent(0, g_lastTouchDown); }
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
    logFile("STEP A glGetString");
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (!ver) { logFile("STEP A-no GL_VERSION skip"); return g_originalSwap ? g_originalSwap(display,surface) : EGL_FALSE; }
    { char b[128]; snprintf(b,sizeof(b),"STEP A GL_VERSION=%.64s", ver); logFile(b); }
    logFile("STEP B GetCurrentContext");
    if (!ImGui::GetCurrentContext()) { logFile("STEP B-no ctx skip"); return g_originalSwap ? g_originalSwap(display,surface) : EGL_FALSE; }
    logFile("STEP C ImGui_ImplOpenGL3_NewFrame");
    ImGui_ImplOpenGL3_NewFrame();
    logFile("STEP D ImGui::NewFrame");
    ImGui::NewFrame();
    logFile("STEP E NewFrame OK");
    // diagnostic: try minimal draw first to prove ImGui/GL works, then full menu
    // SAFE E-CLIENT MENU — bypasses menu.h's crashing CreateTexture/BackGroundDots, keeps Zeff 8-page style
    auto SafeEClientMenu = [&]() {
        // BIGGER: was 2.5/1.5 (960x720) → 2.0/1.35 so categories breathe; still fits 2400x1080
        ImGui::SetNextWindowSize(ImVec2(screenWidth / 2.05f, screenHeight / 1.35f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("E-Client | Minecraft v1.21.111", nullptr, ImGuiWindowFlags_NoResize)) { ImGui::End(); return; }
        g_window = ImGui::GetCurrentWindow();
        // keep Zeff's purple button style, no dots/textures
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 ob = style.Colors[ImGuiCol_Button], oh = style.Colors[ImGuiCol_ButtonHovered], oa = style.Colors[ImGuiCol_ButtonActive];
        style.Colors[ImGuiCol_Button] = ImVec4(0,0,0,0);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.70f,0,1.00f,0.50f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.70f,0,1.00f,0.35f);
        style.FrameBorderSize = 0.0f;
        // WIDER SidePanel: was 150 → 185 so full labels fit
        ImGui::BeginChild("SidePanel", ImVec2(calcResX * 185, 0), false);
        auto SideBtn = [&](const char* lbl, int id){ 
            style.Colors[ImGuiCol_Button] = (pageId==id)? ImVec4(0.70f,0,1.00f,1.00f) : ImVec4(0,0,0,0);
            style.Colors[ImGuiCol_ButtonHovered] = (pageId==id)? ImVec4(0.70f,0,1.00f,1.00f) : ImVec4(0,0,0,0);
            // WIDER buttons: was ImGui::Button(lbl) → fill SidePanel
            float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button(lbl, ImVec2(w, 36 * scaleGlobal))) pageId=id;
            style.Colors[ImGuiCol_Button]=ImVec4(0,0,0,0); style.Colors[ImGuiCol_ButtonHovered]=ImVec4(0,0,0,0);
        };
        SideBtn("Movement",0); SideBtn("Combat",1); SideBtn("Building",2); SideBtn("List",3);
        SideBtn("Visuals",4); SideBtn("Hotkeys",5); SideBtn("Other",6); SideBtn("Info",7);
        ImGui::Spacing(); ImGui::Separator();
        // minimize: title-bar drag already works (ACTION_MOVE fix), this hides to corner
        if (ImGui::Button("— Minimize", ImVec2(ImGui::GetContentRegionAvail().x, 30*scaleGlobal))) {
            // just collapse to title-bar via ImGui, next tap restores
            ImGui::SetWindowCollapsed(!ImGui::IsWindowCollapsed(), ImGuiCond_Always);
        }
        ImGui::EndChild();
        style.Colors[ImGuiCol_Button]=ob; style.Colors[ImGuiCol_ButtonHovered]=oh; style.Colors[ImGuiCol_ButtonActive]=oa; style.FrameBorderSize=2.0f;
        ImGui::SameLine();
        float lh = ImGui::GetWindowHeight(); ImVec2 sv = ImGui::GetCursorScreenPos(); sv.y = ImGui::GetWindowPos().y;
        ImGui::GetWindowDrawList()->AddLine(sv, ImVec2(sv.x, sv.y+lh), ImGui::GetColorU32(ImGuiCol_Separator), scaleGlobal*2.0f);
        ImGui::SameLine(calcResX * 242);
        ImGui::BeginGroup();
        if (pageId==0) { // Movement
            ImGui::Text("Movement"); ImGui::Separator();
            ImGui::Checkbox("Always Sprinting", &alwaysSprinting);
            ImGui::SliderInt("Speed", &speedValue, 1, 50);
            ImGui::Checkbox("Fly / CreativeFly", &creativeFly);
            ImGui::Checkbox("No SlowDown", &noSlowDown);
            ImGui::Checkbox("Noclip", &noclip);
            ImGui::Checkbox("Anti Knockback", &antiKnockback);
        } else if (pageId==1) { // Combat
            ImGui::Text("Combat"); ImGui::Separator();
            ImGui::Checkbox("Kill Aura", &killAura); if (killAura) { ImGui::SliderFloat("Range", &killRange, 1, 10); ImGui::SliderInt("Tick", &maxKillTick, 1, 10); }
            ImGui::Checkbox("Mob Aura", &mobAura);
            ImGui::Checkbox("Anti Bot", &antiBot);
            ImGui::Checkbox("HitBox Expand", &hitBox); if (hitBox) ImGui::SliderFloat("HitBox", &hitBoxValue, 0.6f, 5.0f);
        } else if (pageId==2) { // Building
            ImGui::Text("Building"); ImGui::Separator();
            ImGui::Checkbox("Fast Bridge", &fastBridge);
            ImGui::Checkbox("Block Reach", &blockReach);
            ImGui::Checkbox("Xray Camera", &xrayCam);
            ImGui::Checkbox("No Hurt Cam", &noHurtCam);
        } else if (pageId==3) { // List
            ImGui::Text("List / Players"); ImGui::Separator();
            ImGui::Checkbox("Player Near", &playerNear);
            ImGui::Checkbox("ESP Enabled", &esp);
            ImGui::Text("nearPlayerList: %zu", nearPlayerList.size());
        } else if (pageId==4) { // Visuals
            ImGui::Text("Visuals"); ImGui::Separator();
            ImGui::Checkbox("ESP Box", &espBox); ImGui::Checkbox("ESP Nametag", &espNametag); ImGui::Checkbox("ESP Line", &espLine);
            ImGui::Checkbox("ESP Health", &espHealth); ImGui::Checkbox("ESP Distance", &espDistance);
            ImGui::Checkbox("FullBright", &fullBright); ImGui::Checkbox("No Blur", &noBlur);
            ImGui::Checkbox("Show Position", &showPosition);
        } else if (pageId==5) { // Hotkeys
            ImGui::Text("Hotkeys"); ImGui::Separator(); ImGui::TextWrapped("Hotkeys UI stripped — use in-game buttons.");
            if (ImGui::Button("Panic OFF")) panic=false; ImGui::SameLine(); if (ImGui::Button("Panic ON")) panic=true;
        } else if (pageId==6) { // Other
            ImGui::Text("Other"); ImGui::Separator();
            ImGui::Checkbox("No Cave Vignette", &noCaveVignette); ImGui::Checkbox("No Hurt Cam", &noHurtCam);
            ImGui::Checkbox("Fog Enable", &fogEnable); if (fogEnable) ImGui::SliderInt("Fog State", &fogState, 0, 3);
        } else if (pageId==7) { // Info
            ImGui::Text("E-Client | Minecraft v1.21.111"); ImGui::Separator();
            ImGui::TextWrapped("Maintained by E-Client Team. Free and open — Alextheplayer919/Eclient-Attach.");
            ImGui::Text("Build patched-r59 — safe 8-page (icons/dots restored next)");
            ImGui::Text("Scale %.2f", scaleGlobal);
            if (ImGui::Button("I READ")) readMe=true;
        }
        ImGui::EndGroup();
        ImGui::End();
    };
    logFile("STEP F safe 8-page menu");
    SafeEClientMenu();
    logFile("STEP K safe done");
    logFile("STEP L EndFrame");
    ImGui::EndFrame();
    logFile("STEP M Render");
    ImGui::Render();
    // RenderDrawData does GL — guard it
    logFile("STEP N before RenderDrawData");
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    logFile("STEP O after RenderDrawData");
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
