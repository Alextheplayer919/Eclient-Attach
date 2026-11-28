#!/usr/bin/env python3
"""Apply the minimal in-process adapter to a temporary copy of Apollon."""
import importlib.util
import os
import sys
from pathlib import Path

jni = Path(sys.argv[1])
p = jni / 'main.cpp'
s = p.read_text()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise ValueError(f'expected one source occurrence, got {s.count(old)}: {old[:100]!r}')
    s = s.replace(old, new, 1)
replace('#include <jni.h>', '#include <jni.h>\n#include <atomic>\n#include <android/log.h>')
# Keep an unscaled style for each new ImGui context. The upstream static
# isLoaded flag outlives destroyed contexts and causes tiny controls after a
# remount; scale afresh on every valid resize without compounding the scale.
replace('\tembraceTheDarkness();', '\tembraceTheDarkness();\n    eclientBaseStyle = ImGui::GetStyle();\n    eclientAppliedGuiScale = -1.0f;')
replace('''\tstatic bool isLoaded = false;
\tif (!isLoaded) {
\t\tImGuiStyle& style = ImGui::GetStyle();
\t    scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
\t\tcalcResX = calculateResolutionX(screenWidth, 2480);
\t    style.ScaleAllSizes(scaleGlobal * 3.5);
        io.FontGlobalScale = scaleGlobal;
\t\tstyle.WindowBorderSize                  = scaleGlobal * 2;
        style.ChildBorderSize                   = scaleGlobal * 2;
        style.PopupBorderSize                   = scaleGlobal * 2;
        style.FrameBorderSize                   = scaleGlobal * 2;
        style.TabBorderSize                     = scaleGlobal * 2;
\t\tisLoaded = true;
\t}''', '''    if (screenWidth > 0 && screenHeight > 0) {
        ImGuiStyle& style = ImGui::GetStyle();
        style = eclientBaseStyle;
        scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
        calcResX = calculateResolutionX(screenWidth, 2480);
        style.ScaleAllSizes(scaleGlobal * 3.5f * eclientGuiScale);
        io.FontGlobalScale = scaleGlobal * eclientGuiScale;
        eclientAppliedGuiScale = eclientGuiScale;
        style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = scaleGlobal * 2;
        style.FrameBorderSize = style.TabBorderSize = scaleGlobal * 2;
        // Scrollbars must remain usable on a touchscreen after context recreation.
        style.ScrollbarSize = std::max(style.ScrollbarSize, 18.0f);
        style.GrabMinSize = std::max(style.GrabMinSize, 18.0f);
    }''')
# JNI escapes underscores in Java method names as _1. Rename both the forward
# declaration and definition, otherwise Android cannot resolve imgui_Shutdown.
old_jni = 'Java_com_mojang_minecraftpe_platforms_Platform2_imgui_Shutdown'
if s.count(old_jni) != 2:
    raise ValueError('unexpected imgui_Shutdown JNI declaration count')
s = s.replace(old_jni, 'Java_com_mojang_minecraftpe_platforms_Platform2_imgui_1Shutdown')
replace('bool g_Initialized = false;', '''bool g_Initialized = false;
static ImGuiStyle eclientBaseStyle;
static float eclientGuiScale = 1.0f, eclientAppliedGuiScale = -1.0f;
static std::atomic<int> eclientX{0}, eclientY{0}, eclientW{0}, eclientH{0};''')
replace('        removeDirectory(oxorany("/data/data/") + GameName + oxorany("/app_lib"));\n        saveConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));', '''        static auto lastSave = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastSave >= std::chrono::seconds(5)) {
            std::error_code dirError;
            fs::create_directories(oxorany("/data/data/") + GameName + oxorany("/xal"), dirError);
            saveConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));
            lastSave = now;
        }''')
# Scale is applied from an unscaled base style, before starting the next frame.
replace('        ImGui_ImplOpenGL3_NewFrame();', """        if (screenWidth > 0 && screenHeight > 0 && eclientAppliedGuiScale != eclientGuiScale) {
            ImGuiStyle& style = ImGui::GetStyle();
            style = eclientBaseStyle;
            style.ScaleAllSizes(scaleGlobal * 3.5f * eclientGuiScale);
            io.FontGlobalScale = scaleGlobal * eclientGuiScale;
            style.ScrollbarSize = std::max(style.ScrollbarSize, 18.0f);
            style.GrabMinSize = std::max(style.GrabMinSize, 18.0f);
            eclientAppliedGuiScale = eclientGuiScale;
        }
        ImGui_ImplOpenGL3_NewFrame();""")
replace('        BeginDraw();', '''        BeginDraw();
        eclient_module_menu::drawShortcuts();
        if (g_window) {
            eclientX.store((int)g_window->Pos.x); eclientY.store((int)g_window->Pos.y);
            eclientW.store((int)g_window->Size.x); eclientH.store((int)g_window->Size.y);
        } else { eclientW.store(0); eclientH.store(0); }''')
# Queue touch transitions on the GL thread for ImGui's event processing: directly
# changing MouseDown can collapse a fast DOWN+UP into a single invisible tap.
replace("\tio.MousePos = ImVec2(PosX, PosY);\n\tio.MouseDown[0] = down;", "\tio.AddMousePosEvent(PosX, PosY);\n\tio.AddMouseButtonEvent(0, down);")
replace('#include "menu_utils.h"', '#include "menu_utils.h"\n#include "fly2_adapter.h"\n#include "gatyo_bypass_fly.h"\n#include "placement_forward.h"\n#include "gatyo_aura.h"\n#include "module_menu.h"')
replace('#include "module_menu.h"', '#include "module_menu.h"\nextern "C" JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_setFly2LogPath(JNIEnv* env, jclass, jstring path) { if (!path) return; const char* chars = env->GetStringUTFChars(path, nullptr); if (chars) { eclient_fly2_adapter::setLogPath(chars); env->ReleaseStringUTFChars(path, chars); } }')
replace('#include "module_menu.h"', '#include "preview_adapter.h"\n#include "quick_commands.h"\n#include "module_menu.h"')
replace('#include "module_menu.h"', '''#include "module_menu.h"
extern "C" JNIEXPORT jint JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_takeCommandEdit(JNIEnv*, jclass) { return eclient_quick_commands::editRequest.exchange(-1); }
extern "C" JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getCommandEditText(JNIEnv* env, jclass, jint index) { auto text=eclient_quick_commands::editText(index); return env->NewStringUTF(text.c_str()); }
extern "C" JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_submitCommandEdit(JNIEnv* env, jclass, jint index, jstring value) { if(!value)return; auto text=env->GetStringUTFChars(value,nullptr); if(text){eclient_quick_commands::submitEdit(index,text);env->ReleaseStringUTFChars(value,text);} }''')
replace('#include "module_menu.h"', '#include "module_menu.h"\nextern "C" JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_setPreviewAssets(JNIEnv* env, jclass, jobject assets) { eclient_preview::setAssets(env, assets); }')
replace('#include "module_menu.h"', '#include "module_menu.h"\nextern "C" JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getShortcutRects(JNIEnv* env, jclass) { auto rects=eclient_module_menu::getShortcutRects(); return env->NewStringUTF(rects.c_str()); }')
# Run cleanup even on Apollon's early panic/menu/death exits. The original
# tick runs first so its Creative Fly bookkeeping cannot undo Fly2 this frame.
normal_start = s.index('void *NormalTick(LocalPlayer *thiz) {')
normal_end = s.index('\nvoid *(*old_PlayerAuthInputPacket)', normal_start)
normal = s[normal_start:normal_end]
if normal.count('return old_NormalTick(thiz);') != 4:
    raise ValueError('unexpected Apollon NormalTick exits')
normal = normal.replace('return old_NormalTick(thiz);', 'return eclientFly2TickAndReturn(thiz, false);', 3)
normal = normal.replace('return old_NormalTick(thiz);', 'return eclientFly2TickAndReturn(thiz, true);', 1)
helper = '''static void* eclientFly2TickAndReturn(LocalPlayer* player, bool gameReady) {
    if (gameReady && player) {
        eclient_bypass_fly::MoveInputUpdate(player);
        eclient_bypass_fly::LookTrackBegin(player);
        eclient_bypass_fly::FlyOnNormalTick(player);
        eclient_bypass_fly::LookTrackEnd(player, false);
        eclient_bypass_fly::MoveInputRecord(player);
    } else { eclient_bypass_fly::reset(); eclient_gatyo_aura::reset(); }
    void* result = old_NormalTick(player);
    eclient_fly2_adapter::tick(player, gameReady);
    return result;
}
'''
s = s[:normal_start] + helper + normal + s[normal_end:]

# Gatyo BypassFly observes input before serialization and restores packet bits
# after the game's original writer. The confirmed combat calls remain separate.
replace('\tif (panic) return old_PlayerAuthInputPacket(a1, a2);', '\tif (panic) return old_PlayerAuthInputPacket(a1, a2);\n    eclient_bypass_fly::MoveInputOnAuthPacket(a1);')
replace('\treturn old_PlayerAuthInputPacket(a1, a2);', '    uint64_t flySavedBits[2];\n    eclient_bypass_fly::FlyFilterAuthBits(a1, flySavedBits);\n    void* flyResult = old_PlayerAuthInputPacket(a1, a2);\n    eclient_bypass_fly::FlyRestoreAuthBits(a1, flySavedBits);\n    return flyResult;')
replace('    if (panic) return old_Gravity(a1, a2, a3, a4, a5, a6);', '    if (panic) return old_Gravity(a1, a2, a3, a4, a5, a6);\n    eclient_bypass_fly::MoveInputOnGravity((void*)a4);')

# Gatyo's local-facing writes happen after Apollon's rotation logic, and the
# packet spoof runs after Apollon's yaw spoof, just as the source prioritizes it.
replace('    return old_Intersects(thiz, a2, a3);', '    if (thiz == localPlayer) eclient_gatyo_aura::applyLocal((LocalPlayer*)thiz);\n    return old_Intersects(thiz, a2, a3);')
replace('    uint64_t flySavedBits[2];', '    eclient_gatyo_aura::applyPacket(a1);\n    uint64_t flySavedBits[2];')
replace('\tstd::vector<Mob*> actors = myLevel->getRuntimeActorList();', '\tstd::vector<Mob*> actors = myLevel->getRuntimeActorList();\n    eclient_gatyo_aura::tick(thiz, myLevel, myGameMode, actors);')

# The ARM64 binary's placement entry uses x0-x2, w3, x4-x5 and w6.
# Upstream's six int arguments truncate pointers and drop w6; preserve the
# full registers. Argument semantics and safe synthetic invocation are NOT known.
replace('''int (*old_GameMode_FastBridge)(int a1, int a2, int *a3, int a4, int a5, int a6);
int GameMode_FastBridge(int a1, int a2, int *a3, int a4, int a5, int a6) {
\tif (panic) return old_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
\tif (fastBridge) {
\t\told_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
\t\treturn 0;
\t}
    return old_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
}''', '''eclient::compat::PlacementFn old_GameMode_FastBridge;
std::uint64_t GameMode_FastBridge(std::uintptr_t a0, std::uintptr_t a1, std::uintptr_t a2,
                                  std::uint32_t a3, std::uintptr_t a4, std::uintptr_t a5, std::uint32_t a6) {
    const eclient::compat::PlacementArgs args{a0,a1,a2,a3,a4,a5,a6};
    const auto result = eclient::compat::forwardPlacement(old_GameMode_FastBridge, args);
    // Preserve the original adapter's FastBridge return policy.
    return (!panic && fastBridge) ? 0 : result;
}''')
replace('oxorany("§l§0[§5Apollon Client§0]§r Good Luck, Have Fun!")', 'oxorany("[E-Client] Good luck and have fun!")')
start=s.index('JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getWindowRect(JNIEnv *env, jobject thiz) {')
end=s.index('\nvoid *(*old_Intersects)', start)
s=s[:start]+'''JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getWindowRect(JNIEnv *env, jobject) {
    char result[128];
    snprintf(result, sizeof(result), "%d|%d|%d|%d", eclientX.load(), eclientY.load(), eclientW.load(), eclientH.load());
    return env->NewStringUTF(result);
}
''' + s[end:]
replace('    g_Initialized=false;', '    g_Initialized=false;\n    eclientW.store(0); eclientH.store(0); g_window = nullptr;')
replace('''    DobbyHook(reinterpret_cast<void*>((const void*(*)(...))dlsym(handle, OBFUSCATE("Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler"))), reinterpret_cast<void*>(new_Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler), reinterpret_cast<void**>(&old_Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler));
\thexPatches.NativeKeyHandler = MemoryPatch::createWithHex(MineLib, Offsets64::NativeKeyHandler, "C0 03 5F D6");
\thexPatches.NativeKeyHandler.Modify();''', '''    // The stock key handler is intentionally untouched in the injected build.''')
replace('''        mineMap = KittyMemory::getLibraryMap(MineLib);
\t\thandle = dlopen(OBFUSCATE("libminecraftpe.so"), RTLD_LAZY);
    } while (!mineMap.isValid());''', '''        mineMap = KittyMemory::getLibraryMap(MineLib);
        if (!mineMap.isValid()) usleep(250000);
    } while (!mineMap.isValid());
    handle = dlopen(OBFUSCATE("libminecraftpe.so"), RTLD_LAZY);
    if (!handle || !eclientValidateTarget()) {
        __android_log_print(ANDROID_LOG_ERROR, "EClientApollon", "Target validation failed; no hooks installed");
        return nullptr;
    }''')
# Reuse the checked-in expected-byte list; never derive expected bytes from the running binary.
spec = importlib.util.spec_from_file_location('target', Path(__file__).with_name('check-apollon-target.py'))
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
lines = []
for name, hexbytes in mod.EXPECTED.items():
    data = ','.join('0x'+hexbytes[i:i+2] for i in range(0,len(hexbytes),2))
    lines.append(f'    {{ const unsigned char expected[] = {{{data}}}; auto address = getAbsoluteAddress(MineLib, Offsets64::{name}); if (!address || memcmp((void*)address, expected, sizeof(expected))) return false; }}')
validate = 'static bool eclientValidateTarget() {\n#if defined(__aarch64__)\n'+'\n'.join(lines)+'\n    return true;\n#else\n    return false;\n#endif\n}\n\n'
replace('void *hack_thread(void *) {', validate+'void *hack_thread(void *) {')
replace('    void* result = old_NormalTick(player);', '    if (gameReady) eclient_quick_commands::flush(); else eclient_quick_commands::clearQueue();\n    void* result = old_NormalTick(player);')
# Include the combat modules and console in the default client. An explicit
# ECLIENT_COMBAT_LAB=0 remains available for isolated compatibility testing.
combat_lab = os.environ.get('ECLIENT_COMBAT_LAB', '1') != '0'
if combat_lab:
    replace('#include "gatyo_aura.h"', '#define ECLIENT_COMBAT_LAB 1\n#include "combat_lab.h"\n#include "gatyo_combat.h"\n#include "auto_use.h"\n#include "gatyo_fake.h"\n#include "gatyo_aura.h"')
    replace('\tA64HookFunction((void *)getAbsoluteAddress(MineLib, Offsets64::NormalTick)', '\teclient_fake::install();\n\tA64HookFunction((void *)getAbsoluteAddress(MineLib, Offsets64::NormalTick)')
    replace('    void* result = old_NormalTick(player);', '    if (!gameReady) { eclient_autouse::resetWorld(); eclient_combat_lab::cancelIfArmed(); eclient_gatyo::leaveWorld(); eclient_fake::resetWorld(); }\n    void* result = old_NormalTick(player);')
    replace('    eclient_gatyo_aura::tick(thiz, myLevel, myGameMode, actors);', '    eclient_fake::tick(thiz, myLevel, myBlockSource, playerPos, actors);\n    eclient_autouse::tick(thiz, myGameMode, CI);\n    if (!eclient_autouse::busy()) eclient_gatyo_aura::tick(thiz, myLevel, myGameMode, actors);\n    else if (eclient_gatyo::cpvpActive()) eclient_gatyo::cpvpTrack(0, {});\n    if (!eclient_autouse::busy() && !eclient_gatyo::cpvpActive()) eclient_combat_lab::tick(thiz, myGameMode, playerPos, actors);\n    vec3_t eclientFakeTarget;\n    bool eclientHasFakeTarget = eclient_fake::target(eclientFakeTarget);\n    if (!eclient_autouse::busy()) { eclient_gatyo::tick(thiz, myLevel, myGameMode, myBlockSource, playerPos, CI, eclientHasFakeTarget ? &eclientFakeTarget : nullptr); eclient_gatyo::cpvpTick(thiz, myGameMode, myBlockSource, playerPos, CI, actors); } else { eclient_gatyo::tryTotem(thiz); }')
    replace('if (mobAura) {', 'if (mobAura && !eclient_gatyo::cpvpActive() && !eclient_autouse::busy()) {')
    replace('if (killAura) {', 'if (killAura && !eclient_gatyo::cpvpActive() && !eclient_autouse::busy()) {')
    replace('    const auto result = eclient::compat::forwardPlacement(old_GameMode_FastBridge, args);', '    const auto result = eclient_combat_lab::placementExperiment(old_GameMode_FastBridge, args);\n    if (eclient_autouse::traceUse.load()) eclient_console::log.add("Use trace: useItemOn called (result %llu)", (unsigned long long)result);')
    replace('\tRenderHook();', '\teclient_combat_lab::install();\n\tRenderHook();')
p.write_text(s)
menu = jni / 'menu.h'
m = menu.read_text()
m = m.replace('                        if (xrayDefault) RenderOreESP', '                        eclient_preview::draw(draw_list, CameraPos, currentFov, currentViewMatrix);\n' + ('                        eclient_fake::draw(draw_list, CameraPos, currentFov, currentViewMatrix);\n' if combat_lab else '') + '                        if (xrayDefault) RenderOreESP', 1)
def menu_replace(old, new, expected=1):
    global m
    if m.count(old) != expected:
        raise ValueError(f"unexpected menu source occurrence count: {m.count(old)} for {old[:80]!r}")
    m = m.replace(old, new)
menu_replace('The Apollon Client v4.62 | APK (x64 Bit) | Minecraft v1.21.111', 'E-Client | Apollon v4.62 | Minecraft 1.21.111')
# Panic must still disable modules, but should not make the only menu disappear.
menu_replace('void BeginDraw() {\n    if (!panic) {', 'void BeginDraw() {\n    {')
menu_replace('\t if (!readMe) {', '\t const int eclientColorDepth = ImGui::GetCurrentContext()->ColorStack.Size;\n\t if (!readMe) {')
menu_replace('if (ImGui::Begin(OBFUSCATE("Description"), NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {\n\t\t\t g_window = ImGui::GetCurrentWindow();', 'bool eclientDraw = ImGui::Begin(OBFUSCATE("Description"), NULL, ImGuiWindowFlags_NoResize);\n\t\t g_window = ImGui::GetCurrentWindow();\n\t\t if (eclientDraw) {')
# ImGui::End is mandatory even when a window is collapsed. The upstream source
# omitted it, and popped the title style by enum value instead of count.
menu_replace('\t     }\n\t  } else  {\n     #if defined(__aarch64__)', '\t     }\n         ImGui::PopStyleColor(ImGui::GetCurrentContext()->ColorStack.Size - eclientColorDepth);\n         ImGui::End();\n\t  } else  {\n     #if defined(__aarch64__)')
for title in ('E-Client | Apollon v4.62 | Minecraft 1.21.111', 'The Apollon Client v4.62 | APK (x32 Bit) | Minecraft v1.21.111', 'The Apollon Client v4.62 | APK (x86_64 Bit) | Minecraft v1.21.111'):
    menu_replace(f'if (ImGui::Begin(OBFUSCATE("{title}"), NULL, ImGuiWindowFlags_NoResize)) {{', f'bool eclientDraw = ImGui::Begin(OBFUSCATE("{title}"), NULL, ImGuiWindowFlags_NoResize);')
menu_replace('#endif\n\t\tg_window = ImGui::GetCurrentWindow();\n\t\n\t\tImGui::SetWindowSize', '#endif\n\t\tg_window = ImGui::GetCurrentWindow();\n        if (eclientDraw) {\n\t\tImGui::SetWindowSize')
menu_replace('\t\tImGui::PopStyleVar(ImGuiStyleVar_WindowTitleAlign);\n    }\n\t}\n\t}', '    }\n        ImGui::PopStyleColor(ImGui::GetCurrentContext()->ColorStack.Size - eclientColorDepth);\n        ImGui::End();\n\t}\n        ImGui::PopStyleVar();\n\t}')
# Balance colors inside the upstream list and introductory tabs before their
# corresponding child/tab closes, not merely at the end of the outer window.
menu_replace('\t\t\t\t     ImGui::EndTabItem();\n\t\t\t\t}\n', '\t\t\t\t ImGui::PopStyleColor(2);\n\t\t\t\t ImGui::EndTabItem();\n\t\t\t\t}\n', expected=2)
menu_replace('\t\t\t\t                ImGui::PopID();\n\t\t\t\t\t\t\t}', '\t\t\t\t                ImGui::PopID();\n                                ImGui::PopStyleColor();\n\t\t\t\t\t\t\t}')
menu_replace('\t\t\t\t\t\tImGui::EndChild();\n\t\t\t\t        ImGui::EndTabItem();\n\t\t\t\t    }\n\t\t\t\t\tImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 1, 1));', '\t\t\t\t\t\tImGui::EndChild();\n                        ImGui::PopStyleColor();\n\t\t\t\t        ImGui::EndTabItem();\n\t\t\t\t    }\n\t\t\t\t\tImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 1, 1));')
menu_replace('\t\t\t\t\tImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));\n\t\t\t\t\tif (ImGui::BeginTabItem(OBFUSCATE("Bots")))', '\t\t\t\t\tImGui::PopStyleColor();\n\t\t\t\t\tImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));\n\t\t\t\t\tif (ImGui::BeginTabItem(OBFUSCATE("Bots")))')
menu_replace('\t\t\t\t\tImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));\n\t\t\t\t    ImGui::EndTabBar();', '\t\t\t\t\tImGui::PopStyleColor();\n\t\t\t\t    ImGui::EndTabBar();')
# Remove the old intro and social pages. Source credit remains in Info without
# a startup toast.
intro_start = m.index('\t if (!readMe) {')
intro_end = m.index('\t  } else  {\n     #if defined(__aarch64__)', intro_start)
m = m[:intro_start] + m[intro_end + len('\t  } else  {\n') :]
menu_replace('        ImGui::End();\n\t}\n        ImGui::PopStyleVar();', '        ImGui::End();\n        ImGui::PopStyleVar();')
start_info = m.index('            case 7:')
end_info = m.index('                break;', start_info)
m = m[:start_info] + '            case 7:\n                ImGui::Text("E-Client | Minecraft 1.21.111");\n                ImGui::TextWrapped("Based on Apollon source code by ZEFF.");\n                ImGui::SliderFloat("ClickGUI scale", &eclientGuiScale, 0.70f, 1.40f, "%.2fx");\n                if (ImGui::Button("Reset GUI scale")) eclientGuiScale = 1.0f;\n                ImGui::TextWrapped("Scale lasts for this session.");\n' + m[end_info:]
for social_header in ('YT', 'Telegram', 'Discord'):
    menu_replace(f'#include "Images/{social_header}.h"\n', '')
for icon in ('youtube', 'telegram', 'discord'):
    import re
    m, count = re.subn(r'^.*(?:static GLint ' + icon + r'_icon|if \(!' + icon + r'_icon\)).*\n', '', m, flags=re.M)
    if count != 2: raise ValueError('social icon source changed: ' + icon)
menu_replace('\t\t\t\t        ImGui::Checkbox(OBFUSCATE("Creative Fly"), &creativeFly);', '                        eclient_fly2_adapter::draw();\n\t\t\t\t        ImGui::Checkbox(OBFUSCATE("Creative Fly"), &creativeFly);')
# A horizontal module strip below the title. "Classic" retains the complete
# upstream controls while each mapped module gets its own independent page.
menu_replace('ImGui::SetWindowSize(ImVec2(screenWidth / 2.5, screenHeight / 1.5));', 'ImGui::SetWindowSize(ImVec2(std::min(screenWidth * 0.95f, screenWidth / 2.5f * eclientGuiScale), std::min(screenHeight * 0.95f, screenHeight / 1.5f * eclientGuiScale)));')
menu_replace('ImVec2(calcResX * 150, 0)', 'ImVec2(calcResX * 150 * eclientGuiScale, 0)')
menu_replace('ImGui::SameLine(calcResX * 226);', 'ImGui::SameLine(calcResX * 226 * eclientGuiScale);')
menu_replace('\t\tfloat pointSize = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);', '        eclient_module_menu::row(pageId);\n\t\tfloat pointSize = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);')
menu_replace('        switch (pageId) {', '        if (!eclient_module_menu::page(pageId)) switch (pageId) {')
if combat_lab:
    menu_replace('\t\tif (ImGui::Button(OBFUSCATE("Info"))) pageId = 7;', '''\t\tif (ImGui::Button(OBFUSCATE("Info"))) pageId = 7;
        style.Colors[ImGuiCol_Button] = pageId == 8 ? ImVec4(0.70f, 0.00f, 1.00f, 1.00f) : ImVec4(0, 0, 0, 0);
        style.Colors[ImGuiCol_ButtonHovered] = style.Colors[ImGuiCol_Button];
        if (ImGui::Button("Console")) pageId = 8;''')
    menu_replace('        if (!eclient_module_menu::page(pageId)) switch (pageId) {', '        if (!eclient_module_menu::page(pageId)) switch (pageId) {\n            case 8: eclient_combat_lab::drawConsole(); break;')
# Remove superseded module lists entirely. Preserve non-module List, Hotkeys,
# Info panels; all moved settings now live in the corresponding module pages.
for first, after in ((0, 3), (4, 5), (6, 7)):
    begin = m.index(f'\t\t\tcase {first}:')
    end = m.index(f'\t\t\tcase {after}:' if after != 7 else '            case 7:', begin)
    m = m[:begin] + m[end:]
menu_replace('\t\t\t\tImGui::Checkbox(OBFUSCATE("Anti Bot"), &antiBot);\n', '')
# Hotkey panel has one width push; upstream pushed again instead of popping.
menu_replace('                ImGui::PushItemWidth(0);\n\t\t\t\tImGui::EndChild();', '                ImGui::PopItemWidth();\n\t\t\t\tImGui::EndChild();')
menu.write_text(m)
mk = jni / 'Android.mk'
import shutil
shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'menu' / 'module_menu.h', jni / 'module_menu.h')
if combat_lab:
    for file in ('combat_lab.h', 'live_console.h', 'gatyo_combat.h', 'gatyo_fake.h', 'fake_simulation.h', 'steve_skin.h', 'combat_modes.h'):
        shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'combat-lab' / file, jni / file)
    path = jni / 'module_menu.h'
    menu_source = path.read_text()
    for old, new in (
        ('CONFIG, SHORTCUTS, SHORTCUT_UP, SHORTCUT_DOWN, SCHEMATIC_PREVIEW, QUICK_COMMANDS };', 'CONFIG, SHORTCUTS, SHORTCUT_UP, SHORTCUT_DOWN, SCHEMATIC_PREVIEW, QUICK_COMMANDS, CRYSTAL_AURA_TEST, AUTO_TOTEM_TEST, FAKE_PLAYER_TEST, AUTO_USE_TEST };'),
        ('static Module combat[] = {{"Gatyo KillAura",', 'static Module combat[] = {{"Crystal Aura",nullptr,CRYSTAL_AURA_TEST},{"Auto Use",nullptr,AUTO_USE_TEST},{"Auto Totem",nullptr,AUTO_TOTEM_TEST},{"Fake Player",nullptr,FAKE_PLAYER_TEST},{"Gatyo KillAura",'),
        ('    else if (m.settings == SCHEMATIC_PREVIEW)', '    else if (m.settings == CRYSTAL_AURA_TEST) eclient_gatyo::crystalUI();\n    else if (m.settings == AUTO_USE_TEST) eclient_autouse::ui();\n    else if (m.settings == AUTO_TOTEM_TEST) eclient_gatyo::totemUI();\n    else if (m.settings == FAKE_PLAYER_TEST) eclient_fake::ui();\n    else if (m.settings == SCHEMATIC_PREVIEW)'),
    ):
        if menu_source.count(old) != 1: raise ValueError('unexpected combat lab menu source')
        menu_source = menu_source.replace(old, new, 1)
    menu_source = menu_source.replace('*m.toggle = !active;', '*m.toggle = !active; eclient_console::log.add("Module %s %s", m.name, *m.toggle ? "enabled" : "disabled");')
    path.write_text(menu_source)
shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'compat' / 'placement_forward.h', jni / 'placement_forward.h')
for file in ('quick_commands.h', 'auto_use.h', 'use_state.h'):
    shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'automation' / file, jni / file)
for file in ('preview_adapter.h', 'anchor.h', 'mcstructure.h', 'cube_mesh.h', 'cube_mesh.cpp', 'texture_catalog.h', 'texture_catalog.cpp'):
    shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'schematic' / file, jni / file)
for file in ('fly2.h', 'fly2.cpp', 'fly2_adapter.h', 'fly2_limits.h', 'fly2_calculated.h', 'fly2_probe.h', 'gatyo_bypass_fly.h'):
    shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'fly' / file, jni / file)
m = mk.read_text().replace('LOCAL_SRC_FILES := main.cpp \\', 'LOCAL_SRC_FILES := main.cpp \\\n    cube_mesh.cpp \\\n    texture_catalog.cpp \\\n    fly2.cpp \\').replace('LOCAL_MODULE    := ReverseSigma', 'LOCAL_MODULE    := eclient_host').replace('include $(BUILD_SHARED_LIBRARY)', 'LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384\ninclude $(BUILD_SHARED_LIBRARY)')
mk.write_text(m)

for file in ('gatyo_aura.h', 'gatyo_rotation.h'):
    shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'gatyo-aura' / file, jni / file)
