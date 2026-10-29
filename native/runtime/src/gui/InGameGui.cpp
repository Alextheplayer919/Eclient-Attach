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

// E-Client ClickGUI — rebuilt from clickgui.zip (Apollon extraction) and fully rebranded.
// clickgui.zip was extracted to native/runtime/src/gui/clickgui/, then sanitized:
//   * Window title updated to "E-Client"
//   * Config path updated to EClient.json
//   * All original author branding and external invite links removed
//   * Info page reworded to E-Client branding (no external links)
//   * Description popup reworded to E-Client welcome (no social links)
// Original notes preserved in clickgui/NOTES.txt and clickgui/README.md (sanitized).
// Visual design kept: dark purple theme (embraceTheDarkness), side panel with pageId, tab bars,
// scrollable child with AlwaysVerticalScrollbar for touch, separators, background dots inspiration.

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};
std::atomic_bool g_collapsed{true}; // start minimized — only header visible, tap to expand

struct TouchSample { float x{0}; float y{0}; int action{0}; };
std::mutex g_touchMutex;
std::vector<TouchSample> g_touchQueue;
float g_lastTouchX = -1.0f;
float g_lastTouchY = -1.0f;
bool g_lastTouchDown = false;

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_originalSwap = nullptr;
std::atomic<int> g_physicalWidth{0};
std::atomic<int> g_physicalHeight{0};

std::mutex g_rectMutex;
ImVec2 g_lastWindowPos{0, 0};
ImVec2 g_lastWindowSize{0, 0};

// ClickGUI state — mirrors Apollon pageId but E-Client pages
static int g_pageId = 0; // 0 Movement, 1 Combat, 2 Visuals, 3 Player, 4 Info

// ---------- Helpers (inspired by menu_utils.h but self-contained) ----------
static void DrawSeparatorToEdge() {
    ImGui::Spacing();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 p1 = ImVec2(winPos.x + winSize.x - 8, p0.y);
    p0.x = winPos.x + 8;
    ImGui::GetWindowDrawList()->AddLine(p0, p1, ImGui::GetColorU32(ImGuiCol_Separator), 1.5f);
    ImGui::Spacing();
}

// embraceTheDarkness from clickgui/dark_theme.h — purple/black E-Client theme
static void applyEClientTheme() {
    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]               = ImVec4(0.07f, 0.07f, 0.09f, 0.92f);
    c[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]                = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    c[ImGuiCol_Border]                 = ImVec4(0.55f, 0.00f, 0.85f, 0.70f);
    c[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    c[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.12f, 0.18f, 0.60f);
    c[ImGuiCol_FrameBgHovered]         = ImVec4(0.45f, 0.15f, 0.65f, 0.55f);
    c[ImGuiCol_FrameBgActive]          = ImVec4(0.55f, 0.10f, 0.85f, 0.85f);
    c[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.06f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.08f, 0.16f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.06f, 0.11f, 0.55f);
    c[ImGuiCol_CheckMark]              = ImVec4(0.60f, 0.20f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.20f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive]       = ImVec4(0.70f, 0.30f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                 = ImVec4(0.18f, 0.14f, 0.22f, 0.60f);
    c[ImGuiCol_ButtonHovered]          = ImVec4(0.45f, 0.15f, 0.65f, 0.70f);
    c[ImGuiCol_ButtonActive]           = ImVec4(0.55f, 0.10f, 0.85f, 0.80f);
    c[ImGuiCol_Header]                 = ImVec4(0.25f, 0.15f, 0.32f, 0.55f);
    c[ImGuiCol_HeaderHovered]          = ImVec4(0.45f, 0.15f, 0.65f, 0.55f);
    c[ImGuiCol_HeaderActive]           = ImVec4(0.55f, 0.10f, 0.85f, 0.65f);
    c[ImGuiCol_Separator]              = ImVec4(0.55f, 0.00f, 0.85f, 0.85f);
    c[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TabHovered]             = ImVec4(0.45f, 0.15f, 0.65f, 0.45f);
    c[ImGuiCol_TabActive]              = ImVec4(0.45f, 0.10f, 0.70f, 0.95f);

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding   = ImVec2(8, 8);
    s.FramePadding    = ImVec2(8, 4);
    s.ItemSpacing     = ImVec2(8, 6);
    s.ScrollbarSize   = 26.0f; // fat for finger
    s.GrabMinSize     = 12.0f;
    s.WindowBorderSize = 1.5f;
    s.ChildBorderSize  = 1.0f;
    s.FrameBorderSize  = 1.0f;
    s.TabBorderSize    = 1.0f;
    s.WindowRounding   = 8.0f;
    s.FrameRounding    = 5.0f;
    s.ScrollbarRounding= 8.0f;
    s.GrabRounding     = 5.0f;
    s.TabRounding      = 5.0f;
}

// ---------- Module helpers ----------
static bool toggleModule(const std::string& name, bool& enabled) {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    if (ImGui::Checkbox(name.c_str(), &enabled)) {
        if (!mgr.setEnabled(name, enabled)) {
            enabled = mgr.enabled(name);
            return false;
        }
        return true;
    }
    return false;
}

// ---------- Pages (E-Client mapping of Apollon 8 pages -> 5 pages) ----------
static void drawMovementPage() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    ImGui::BeginChild("##movement_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("Movement — hooks run in LocalPlayer::normalTick");
    DrawSeparatorToEdge();

    if (ImGui::BeginTabBar("##movement_tabs")) {
        if (ImGui::BeginTabItem("Speed & Sprint")) {
            bool v = mgr.enabled("Speed"); if (toggleModule("Speed", v)) {}
            if (v) ImGui::TextDisabled("Validated 1.21.111 movement routine");
            bool sprint = mgr.enabled("AlwaysSprint"); if (toggleModule("AlwaysSprint", sprint)) {}
            bool step = mgr.enabled("Step"); if (toggleModule("Step", step)) {}
            if (step) ImGui::TextDisabled("Auto-step 1.0625 when enabled");
            DrawSeparatorToEdge();
            bool noSlow = mgr.enabled("NoSlowDown"); if (toggleModule("NoSlowDown", noSlow)) {}
            bool slowTrig = mgr.enabled("SlowDownTriggers"); if (toggleModule("SlowDownTriggers", slowTrig)) {}
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Fly")) {
            bool fly = mgr.enabled("Fly"); if (toggleModule("Fly", fly)) {}
            if (fly) ImGui::TextDisabled("AbilityMayFly + Flying on layer 2");
            bool flyx = mgr.enabled("FlyX"); if (toggleModule("FlyX", flyx)) {}
            if (flyx) {
                ImGui::Indent(10);
                ImGui::TextDisabled("FlyX — glide/bypass planner (spiral_flyx)");
                float h = eclient_runtime::modules::gameplay::getFlyXUpH();
                float v = eclient_runtime::modules::gameplay::getFlyXUpV();
                if (ImGui::SliderFloat("Up H Factor", &h, 0.1f, 2.0f, "%.2fx")) eclient_runtime::modules::gameplay::setFlyXUpH(h);
                if (ImGui::SliderFloat("Up V Factor", &v, 0.1f, 2.0f, "%.2fx")) eclient_runtime::modules::gameplay::setFlyXUpV(v);
                ImGui::TextDisabled("Bypass phase 1.09-1.15, glide -0.1266");
                ImGui::Unindent(10);
            }
            bool noclip = mgr.enabled("Noclip"); if (toggleModule("Noclip", noclip)) {}
            bool antiKb = mgr.enabled("AntiKnockback"); if (toggleModule("AntiKnockback", antiKb)) {}
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Other")) {
            bool noWater = mgr.enabled("NoWaterDrown"); if (toggleModule("NoWaterDrown", noWater)) {}
            bool noLava = mgr.enabled("NoLavaDrown"); if (toggleModule("NoLavaDrown", noLava)) {}
            ImGui::TextDisabled("Creative-only drown hooks (fail-closed)");
            DrawSeparatorToEdge();
            ImGui::TextDisabled("More movement modules pending verification.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

static void drawCombatPage() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    ImGui::BeginChild("##combat_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("Combat — KillAura / TargetLock");
    DrawSeparatorToEdge();

    if (ImGui::BeginTabBar("##combat_tabs")) {
        if (ImGui::BeginTabItem("KillAura")) {
            bool ka = mgr.enabled("KillAura"); if (toggleModule("KillAura", ka)) {}
            if (ka) {
                ImGui::Indent(8);
                float kr = eclient_runtime::modules::gameplay::getKillRange();
                float rr = eclient_runtime::modules::gameplay::getRotationsRange();
                if (ImGui::SliderFloat("Kill Range", &kr, 1.0f, 10.0f, "%.1f")) eclient_runtime::modules::gameplay::setKillRange(kr);
                if (ImGui::SliderFloat("Rotations Range", &rr, 1.0f, 15.0f, "%.1f")) eclient_runtime::modules::gameplay::setRotationsRange(rr);
                int at = eclient_runtime::modules::gameplay::getAuraType();
                const char* names[] = {"Single","Nearest","Multi"};
                if (ImGui::Combo("Aura Type", &at, names, 3)) eclient_runtime::modules::gameplay::setAuraType(at);
                int kd = eclient_runtime::modules::gameplay::getMaxKillTick();
                if (ImGui::SliderInt("Kill Delay", &kd, 1, 20)) eclient_runtime::modules::gameplay::setMaxKillTick(kd);
                bool sw = eclient_runtime::modules::gameplay::getSwing();
                if (ImGui::Checkbox("Swing", &sw)) eclient_runtime::modules::gameplay::setSwing(sw);
                bool ro = eclient_runtime::modules::gameplay::getRotations();
                if (ImGui::Checkbox("Rotations", &ro)) eclient_runtime::modules::gameplay::setRotations(ro);
                ImGui::SameLine();
                bool hc = eclient_runtime::modules::gameplay::getHurtCheck();
                if (ImGui::Checkbox("HurtCheck", &hc)) eclient_runtime::modules::gameplay::setHurtCheck(hc);
                ImGui::Unindent(8);
                DrawSeparatorToEdge();
                ImGui::TextDisabled("Apollon full logic: Single/Nearest/Multi + HurtTime check");
            }
            bool tl = mgr.enabled("TargetLock"); if (toggleModule("TargetLock", tl)) {}
            if (tl) ImGui::TextDisabled("Locks yaw to KillAura target (requires KillAura)");

            DrawSeparatorToEdge();
            bool crit = mgr.enabled("CriticalHit"); if (toggleModule("CriticalHit", crit)) {}
            if (crit) ImGui::TextDisabled("Attack-state hook — pending verification");

            bool acm = mgr.enabled("AutoClickMine"); if (toggleModule("AutoClickMine", acm)) {}
            if (acm) ImGui::TextDisabled("Skips mining cooldown routine");

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Info")) {
            ImGui::Text("KillAura scans getRuntimeActorList, filters isAttackable + health>0");
            ImGui::TextWrapped("Rotations Range is scan radius; Kill Range is attack radius. KillRange <= RotationsRange enforced.");
            ImGui::TextWrapped("Supported: Single (first), Nearest (closest), Multi (all in range per tick with delay).");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

static void drawVisualsPage() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    ImGui::BeginChild("##visuals_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("Visuals — brightness, camera, ESP");
    DrawSeparatorToEdge();

    if (ImGui::BeginTabBar("##visuals_tabs")) {
        if (ImGui::BeginTabItem("Patches")) {
            bool fb = mgr.enabled("FullBright"); if (toggleModule("FullBright", fb)) {}
            bool hurt = mgr.enabled("NoHurtCam"); if (toggleModule("NoHurtCam", hurt)) {}
            bool blur = mgr.enabled("NoBlur"); if (toggleModule("NoBlur", blur)) {}
            bool vign = mgr.enabled("NoCaveVignette"); if (toggleModule("NoCaveVignette", vign)) {}
            bool dist = mgr.enabled("NoCamDistortion"); if (toggleModule("NoCamDistortion", dist)) {}
            ImGui::SameLine();
            bool boat = mgr.enabled("NoBoatRotation"); if (toggleModule("NoBoatRotation", boat)) {}
            bool sleep = mgr.enabled("NoCamSleep"); if (toggleModule("NoCamSleep", sleep)) {}
            bool place = mgr.enabled("PlaceCamera"); if (toggleModule("PlaceCamera", place)) {}
            bool xrayCam = mgr.enabled("XrayCameraThird"); if (toggleModule("XrayCameraThird", xrayCam)) {}
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ESP")) {
            bool esp = mgr.enabled("ESP"); if (toggleModule("ESP", esp)) {}
            if (esp) ImGui::TextDisabled("World-to-screen — pending verification (offsets: ViewMatrix, LevelRenderer)");
            else ImGui::TextDisabled("Enable ESP to show actor boxes (when verified)");
            DrawSeparatorToEdge();
            ImGui::TextWrapped("ESP shares KillAura's actor enumeration; will draw boxes/lines when hooks are verified.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

static void drawPlayerPage() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    ImGui::BeginChild("##player_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("Player — inventory & camera tweaks");
    DrawSeparatorToEdge();
    bool emote = mgr.enabled("NoEmoteCooldown"); if (toggleModule("NoEmoteCooldown", emote)) {}
    ImGui::TextDisabled("More player modules pending verification.");
    ImGui::EndChild();
}

static void drawInfoPage() {
    ImGui::BeginChild("##info_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::Text("E-Client — Minecraft Bedrock 1.21.111");
    DrawSeparatorToEdge();
    ImGui::Text("Rebranded from Apollon extraction (clickgui.zip)");
    ImGui::Text("Original author references and external invite links");
    ImGui::Text("have been removed — this is E-Client.");
    DrawSeparatorToEdge();
    ImGui::Text("Build: 1.21.111 ARM64 (patched)");
    ImGui::Text("Runtime: libminecraftpe.so SHA-pinned, fail-closed patches");
    ImGui::Text("Native: eclient_host + eclient_runtime (dual lib, shared flyx_core)");
    DrawSeparatorToEdge();
    ImGui::TextWrapped("This client is free and open. Do not buy from resellers.");
    DrawSeparatorToEdge();
    ImGui::TextDisabled("GitHub: Alextheplayer919/Eclient-Attach");
    ImGui::TextDisabled("Docs: README, SOURCE_INVENTORY, APOLLON_RECOVERED");
    ImGui::TextDisabled("Config: /sdcard/Download/EClient.json");
    DrawSeparatorToEdge();
    ImGui::Text("Made by E-Client Team");
    ImGui::EndChild();
}

// Main draw — side panel layout inspired by clickgui/menu.h SidePanel (pageId-driven)
static void drawEClientPanel() {
    // Page selector (left) + content (right) — same idea as Apollon clickgui
    const float sideW = 110.0f;
    const char* pages[] = {"Movement","Combat","Visuals","Player","Info"};
    // Left side panel
    ImGui::BeginChild("##side", ImVec2(sideW, 0), false);
    for (int i = 0; i < 5; ++i) {
        bool selected = (g_pageId == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.10f, 0.70f, 1.00f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.14f, 0.22f, 0.45f));
        if (ImGui::Button(pages[i], ImVec2(-1, 32))) g_pageId = i;
        ImGui::PopStyleColor(1);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    // Vertical separator
    ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(sp.x-4, sp.y-10), ImVec2(sp.x-4, sp.y+ImGui::GetWindowHeight()), ImGui::GetColorU32(ImGuiCol_Separator), 1.5f);
    ImGui::SameLine();

    ImGui::BeginGroup();
    switch (g_pageId) {
        case 0: drawMovementPage(); break;
        case 1: drawCombatPage(); break;
        case 2: drawVisualsPage(); break;
        case 3: drawPlayerPage(); break;
        case 4: drawInfoPage(); break;
        default: drawInfoPage(); break;
    }
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (g_ready.load()) {
        EGLint width = 0, height = 0;
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
            applyEClientTheme();
            ImGuiStyle& st = ImGui::GetStyle();
            st.TouchExtraPadding = ImVec2(6,6);
            ImGui_ImplOpenGL3_Init("#version 300 es");
            __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client ClickGUI (rebranded from clickgui.zip) initialized");
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);
        io.FontGlobalScale = std::max(1.4f, (float)std::min(width,height) / 380.0f);

        {
            std::lock_guard lock(g_touchMutex);
            if (!g_touchQueue.empty()) {
                for (auto &s : g_touchQueue) {
                    float sx = s.x * scaleX, sy = s.y * scaleY;
                    g_lastTouchX = sx; g_lastTouchY = sy;
                    io.AddMousePosEvent(sx, sy);
                    if (s.action == 0) { g_lastTouchDown = true; io.AddMouseButtonEvent(0, true); }
                    else if (s.action == 2) { g_lastTouchDown = false; io.AddMouseButtonEvent(0, false); }
                }
                g_touchQueue.clear();
            } else if (g_lastTouchDown) {
                io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                io.AddMouseButtonEvent(0, true);
            } else if (g_lastTouchX >= 0) {
                io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                io.AddMouseButtonEvent(0, false);
            } else {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        const bool collapsed = g_collapsed.load();
        const float winW = collapsed ? 270.0f : std::min(620.0f, (float)width * 0.94f);
        const float winH = collapsed ? 44.0f  : std::min(560.0f, (float)height * 0.86f);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2((float)width*0.03f, (float)height*0.05f), ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (collapsed) flags |= ImGuiWindowFlags_NoResize;

        bool open = true;
        if (ImGui::Begin("E-Client", &open, flags)) {
            {
                std::lock_guard l(g_rectMutex);
                g_lastWindowPos = ImGui::GetWindowPos();
                g_lastWindowSize = ImGui::GetWindowSize();
            }

            // Header row — tap to expand/collapse (replaces old volume-up toggle)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f,0.08f,0.16f,0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f,0.15f,0.65f,0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f,0.10f,0.85f,0.95f));
            std::string hdr = collapsed ? "E-Client  [ + ]  Tap to expand##hdr" : "E-Client  [ - ]  Tap to minimize##hdr";
            if (ImGui::Button(hdr.c_str(), ImVec2(-1, 0))) g_collapsed.store(!collapsed);
            ImGui::PopStyleColor(3);

            if (!collapsed) {
                auto snap = eclient_runtime::GameBridge::instance().snapshot();
                ImGui::TextDisabled("MC: %s  |  %s", snap.libraryLoaded ? "YES" : "NO",
                    snap.buildId.empty() ? "unknown" : snap.buildId.c_str());
                DrawSeparatorToEdge();
                drawEClientPanel();
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

void toggleMenu() { g_collapsed.store(!g_collapsed.load()); }
void toggleCollapsed() { g_collapsed.store(!g_collapsed.load()); }
void setCollapsed(bool c) { g_collapsed.store(c); }
bool isCollapsed() { return g_collapsed.load(); }
bool isTouchOnGui(float x, float y) {
    std::lock_guard l(g_rectMutex);
    if (g_lastWindowSize.x <=0 || g_lastWindowSize.y <=0) return false;
    return x >= g_lastWindowPos.x && x <= g_lastWindowPos.x + g_lastWindowSize.x &&
           y >= g_lastWindowPos.y && y <= g_lastWindowPos.y + g_lastWindowSize.y;
}
bool menuOpen() { return !g_collapsed.load(); }
void setPhysicalWindowSize(int w,int h){ g_physicalWidth.store(w); g_physicalHeight.store(h); }
void submitTouch(float x,float y,int action){
    std::lock_guard l(g_touchMutex);
    if (g_touchQueue.size()>64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin()+32);
    g_touchQueue.push_back({x,y,action});
}
bool initialize() {
    std::lock_guard l(g_mutex);
    if (g_ready.load()) return true;
    if (!hookSwapBuffers()) { __android_log_print(ANDROID_LOG_ERROR,TAG,"eglSwapBuffers hook failed"); return false; }
    g_ready.store(true); g_collapsed.store(true);
    __android_log_print(ANDROID_LOG_INFO,TAG,"E-Client host ready (ClickGUI from clickgui.zip, rebranded, no volume key)");
    return true;
}
void shutdown(){
    std::lock_guard l(g_mutex);
    g_ready.store(false); g_collapsed.store(true);
    { std::lock_guard tl(g_touchMutex); g_touchQueue.clear(); }
    g_lastTouchX=-1; g_lastTouchY=-1; g_lastTouchDown=false;
    if (g_imguiReady.exchange(false)){ ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); }
}
} // namespace eclient_runtime::host::InGameGui
