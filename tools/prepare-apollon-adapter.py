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
        if (!panic && g_window) {
            eclientX.store((int)g_window->Pos.x); eclientY.store((int)g_window->Pos.y);
            eclientW.store((int)g_window->Size.x); eclientH.store((int)g_window->Size.y);
        } else { eclientW.store(0); eclientH.store(0); }''')
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
m = menu.read_text().replace('The Apollon Client v4.62 | APK (x64 Bit) | Minecraft v1.21.111', 'E-Client | Apollon v4.62 | Minecraft 1.21.111')
menu.write_text(m)
mk = jni / 'Android.mk'
m = mk.read_text().replace('LOCAL_MODULE    := ReverseSigma', 'LOCAL_MODULE    := eclient_host').replace('include $(BUILD_SHARED_LIBRARY)', 'LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384\ninclude $(BUILD_SHARED_LIBRARY)')
mk.write_text(m)
