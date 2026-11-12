#!/usr/bin/env python3
"""Apply the minimal in-process adapter to a temporary copy of Apollon."""
import importlib.util
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
# JNI escapes underscores in Java method names as _1. Rename both the forward
# declaration and definition, otherwise Android cannot resolve imgui_Shutdown.
old_jni = 'Java_com_mojang_minecraftpe_platforms_Platform2_imgui_Shutdown'
if s.count(old_jni) != 2:
    raise ValueError('unexpected imgui_Shutdown JNI declaration count')
s = s.replace(old_jni, 'Java_com_mojang_minecraftpe_platforms_Platform2_imgui_1Shutdown')
replace('bool g_Initialized = false;', '''bool g_Initialized = false;
static std::atomic<int> eclientX{0}, eclientY{0}, eclientW{0}, eclientH{0};''')
replace('        removeDirectory(oxorany("/data/data/") + GameName + oxorany("/app_lib"));\n        saveConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));', '''        static auto lastSave = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastSave >= std::chrono::seconds(5)) {
            std::error_code dirError;
            fs::create_directories(oxorany("/data/data/") + GameName + oxorany("/xal"), dirError);
            saveConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));
            lastSave = now;
        }''')
replace('        BeginDraw();', '''        BeginDraw();
        if (g_window) {
            eclientX.store((int)g_window->Pos.x); eclientY.store((int)g_window->Pos.y);
            eclientW.store((int)g_window->Size.x); eclientH.store((int)g_window->Size.y);
        } else { eclientW.store(0); eclientH.store(0); }''')
# Queue touch transitions on the GL thread for ImGui's event processing: directly
# changing MouseDown can collapse a fast DOWN+UP into a single invisible tap.
replace("\tio.MousePos = ImVec2(PosX, PosY);\n\tio.MouseDown[0] = down;", "\tio.AddMousePosEvent(PosX, PosY);\n\tio.AddMouseButtonEvent(0, down);")
replace('#include "menu_utils.h"', '#include "menu_utils.h"\n#include "fly2_adapter.h"\n#include "module_menu.h"')
replace('#include "module_menu.h"', '#include "module_menu.h"\nextern "C" JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_setFly2LogPath(JNIEnv* env, jclass, jstring path) { if (!path) return; const char* chars = env->GetStringUTFChars(path, nullptr); if (chars) { eclient_fly2_adapter::setLogPath(chars); env->ReleaseStringUTFChars(path, chars); } }')
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
    void* result = old_NormalTick(player);
    eclient_fly2_adapter::tick(player, gameReady);
    return result;
}
'''
s = s[:normal_start] + helper + normal + s[normal_end:]

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
p.write_text(s)
menu = jni / 'menu.h'
m = menu.read_text()
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
# Remove the old intro and social pages, keeping the one-time startup credit in
# the Android lifecycle adapter. Keep only E-Client/version information in Info.
intro_start = m.index('\t if (!readMe) {')
intro_end = m.index('\t  } else  {\n     #if defined(__aarch64__)', intro_start)
m = m[:intro_start] + m[intro_end + len('\t  } else  {\n') :]
menu_replace('        ImGui::End();\n\t}\n        ImGui::PopStyleVar();', '        ImGui::End();\n        ImGui::PopStyleVar();')
start_info = m.index('            case 7:')
end_info = m.index('                break;', start_info)
m = m[:start_info] + '            case 7:\n                ImGui::Text("E-Client | Minecraft 1.21.111");\n' + m[end_info:]
for social_header in ('YT', 'Telegram', 'Discord'):
    menu_replace(f'#include "Images/{social_header}.h"\n', '')
for icon in ('youtube', 'telegram', 'discord'):
    import re
    m, count = re.subn(r'^.*(?:static GLint ' + icon + r'_icon|if \(!' + icon + r'_icon\)).*\n', '', m, flags=re.M)
    if count != 2: raise ValueError('social icon source changed: ' + icon)
menu_replace('\t\t\t\t        ImGui::Checkbox(OBFUSCATE("Creative Fly"), &creativeFly);', '                        eclient_fly2_adapter::draw();\n\t\t\t\t        ImGui::Checkbox(OBFUSCATE("Creative Fly"), &creativeFly);')
# A horizontal module strip below the title. "Classic" retains the complete
# upstream controls while each mapped module gets its own independent page.
menu_replace('\t\tfloat pointSize = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);', '        eclient_module_menu::row(pageId);\n\t\tfloat pointSize = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);')
menu_replace('        switch (pageId) {', '        if (!eclient_module_menu::page(pageId)) switch (pageId) {')
menu.write_text(m)
mk = jni / 'Android.mk'
import shutil
shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'menu' / 'module_menu.h', jni / 'module_menu.h')
for file in ('fly2.h', 'fly2.cpp', 'fly2_adapter.h', 'fly2_limits.h', 'fly2_calculated.h', 'fly2_probe.h'):
    shutil.copyfile(Path(__file__).resolve().parent.parent / 'apollon-adapter' / 'fly' / file, jni / file)
m = mk.read_text().replace('LOCAL_SRC_FILES := main.cpp \\', 'LOCAL_SRC_FILES := main.cpp \\\n    fly2.cpp \\').replace('LOCAL_MODULE    := ReverseSigma', 'LOCAL_MODULE    := eclient_host').replace('include $(BUILD_SHARED_LIBRARY)', 'LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384\ninclude $(BUILD_SHARED_LIBRARY)')
mk.write_text(m)
