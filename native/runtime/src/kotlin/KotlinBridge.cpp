#include "kotlin/KotlinBridge.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "modules/FlyInput.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"
#include "profile/PatchManager.h"

// ============================================================================
// Kotlin module bridge — lets dex-compiled Kotlin modules (see kotlin-modules/
// in the repo) run alongside the native C++ modules with zero interference:
//
//   * the SAME ModuleManager entry drives the menu toggle (a Kotlin module is
//     just a ModuleState with kotlin=true), so there is exactly one source of
//     truth for "is it enabled" — the toggle is mirrored into Kotlin;
//   * the SAME tick (melody onNormalTick, world-stability gated) dispatches
//     Kotlin modules via one JNI static call per tick;
//   * context services (position/velocity writes, the ▲/▼ fly buttons, the
//     accel-residue input direction) are JNI natives here, using the exact
//     same validated offsets as the C++ modules;
//   * fly-family exclusivity (Fly C++ ⇄ MotionFly Kotlin) is enforced in
//     ModuleManager so two modules never fight over the velocity field.
//
// Class bind discipline: FindClass from an arbitrary native thread on Android
// uses the wrong classloader, so binding happens during nativeTouchReady —
// a Java-initiated call with the application classloader on the frame.
// ============================================================================

namespace eclient_runtime::kotlinbridge {
namespace {
constexpr const char* TAG = "EClientKotlin";

JavaVM* g_vm = nullptr;
std::atomic<bool> g_bound{false};
std::mutex g_attachMutex;

jclass g_registryClass = nullptr;      // global ref — com.rubidiumclient.modules.ModuleRegistry
jmethodID g_tickMid = nullptr;         // tick()V
jmethodID g_setEnabledMid = nullptr;   // setEnabled(Ljava/lang/String;Z)V

// Per-tick context for Kotlin module context-natives.
std::atomic<void*> g_player{nullptr};

struct Vec3 { float x, y, z; };

void* g_lastPlayerForResidue = nullptr;
Vec3 g_prevOut{0.0f, 0.0f, 0.0f}; // last velocity WE wrote via setVelocity
bool g_havePrevOut = false;

using namespace eclient_runtime::profile::mc_1_21_111;

Vec3* velocityPtr(void* actor) {
    if (!actor) return nullptr;
    void* sv = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
    if (!sv) return nullptr;
    return reinterpret_cast<Vec3*>(static_cast<char*>(sv) + VelocityField);
}

bool getPos(void* actor, Vec3& out) {
    if (!actor) return false;
    void* sv = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(actor) + GetPositionField);
    if (!sv) return false;
    const auto p = static_cast<float*>(sv);
    out = {p[0], p[1], p[2]};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

// JNI env is thread-scoped; the game tick thread stays attached for the
// whole session so dispatch costs exactly one CallStaticVoidMethod per tick.
JNIEnv* jniEnv() {
    if (!g_vm) return nullptr;
    static thread_local JNIEnv* tls_env = []() -> JNIEnv* {
        JNIEnv* e = nullptr;
        if (!g_vm) return nullptr;
        if (g_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) return e;
        if (g_vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
        return e;
    }();
    if (!tls_env && g_vm->GetEnv(reinterpret_cast<void**>(&tls_env), JNI_VERSION_1_6) != JNI_OK) {
        tls_env = nullptr;
    }
    return tls_env;
}
} // namespace

// ── Kotlin tick gate ───────────────────────────────────────────────────────
// Written from the Kotlin side (NativeBridge.setKotlinTickActive) whenever its
// enable state changes, read once per game tick by the native game-thread hook.
// Before r25 this was inferred by scanning the native ModuleManager for mirror
// entries with kotlin=true — i.e. a duplicated copy of the Kotlin registry's
// state, which is exactly how the two registries drifted apart. One writer now.
std::atomic<bool> g_kotlinTickActive{false};

void setJavaVm(JavaVM* vm) { g_vm = vm; }

void bind(JNIEnv* env) {
    if (g_bound.load() || !env) return;
    jclass cls = env->FindClass("com/rubidiumclient/modules/ModuleRegistry");
    if (!cls) {
        // Kotlin module layer not present in this build (older APK): inert.
        if (env->ExceptionCheck()) env->ExceptionClear();
        __android_log_print(ANDROID_LOG_WARN, TAG,
            "ModuleRegistry class not found — Kotlin modules unavailable");
        return;
    }
    g_registryClass = static_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);
    g_tickMid = env->GetStaticMethodID(g_registryClass, "tick", "()V");
    g_setEnabledMid = env->GetStaticMethodID(g_registryClass, "setEnabled",
                                             "(Ljava/lang/String;Z)V");
    if (!g_tickMid || !g_setEnabledMid) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        __android_log_print(ANDROID_LOG_WARN, TAG, "ModuleRegistry methods missing");
        return;
    }
    g_bound = true;
    __android_log_print(ANDROID_LOG_INFO, TAG, "Kotlin module registry bound");
}

void setModuleEnabled(const char* name, bool enabled) {
    if (!g_bound.load() || !g_setEnabledMid) return;
    JNIEnv* env = jniEnv();
    if (!env) return;
    jstring jname = env->NewStringUTF(name ? name : "");
    env->CallStaticVoidMethod(g_registryClass, g_setEnabledMid, jname,
                              enabled ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(jname);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_WARN, TAG, "Kotlin setEnabled threw");
    }
}

void dispatchTick(void* localPlayer) {
    g_player.store(localPlayer);
    if (localPlayer != g_lastPlayerForResidue) {
        g_lastPlayerForResidue = localPlayer;
        g_prevOut = {0.0f, 0.0f, 0.0f};
        g_havePrevOut = false;
    }
    if (!g_bound.load() || !g_tickMid) { g_player.store(nullptr); return; }
    JNIEnv* env = jniEnv();
    if (env) {
        env->CallStaticVoidMethod(g_registryClass, g_tickMid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Kotlin tick threw");
        }
    }
    g_player.store(nullptr);
}

bool anyKotlinModuleEnabled() {
    // Cached answer from the Kotlin registry (see g_kotlinTickActive).
    return g_kotlinTickActive.load();
}

} // namespace eclient_runtime::kotlinbridge

// ============================================================================
// Module context natives — called FROM Kotlin during dispatchTick.
// ============================================================================

extern "C" {

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_getPosition(
    JNIEnv* env, jobject /*self*/, jfloatArray out) {
    using namespace eclient_runtime::kotlinbridge;
    if (env->GetArrayLength(out) < 3) return JNI_FALSE;
    Vec3 p{};
    if (!getPos(g_player.load(), p)) return JNI_FALSE;
    const jfloat v[3] = {p.x, p.y, p.z};
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_getVelocity(
    JNIEnv* env, jobject /*self*/, jfloatArray out) {
    using namespace eclient_runtime::kotlinbridge;
    if (env->GetArrayLength(out) < 3) return JNI_FALSE;
    Vec3* vel = velocityPtr(g_player.load());
    if (!vel) return JNI_FALSE;
    const jfloat v[3] = {vel->x, vel->y, vel->z};
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_rubidiumclient_modules_NativeBridge_setVelocity(
    JNIEnv* /*env*/, jobject /*self*/, jfloat x, jfloat y, jfloat z) {
    using namespace eclient_runtime::kotlinbridge;
    void* player = g_player.load();
    Vec3* vel = velocityPtr(player);
    if (!vel) return;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (std::fabs(x) > 100.0f || std::fabs(y) > 100.0f || std::fabs(z) > 100.0f) return;
    vel->x = x; vel->y = y; vel->z = z;
    g_prevOut = {x, y, z};
    g_havePrevOut = true;
}

// World-space horizontal input direction from the vanilla accel residue
// (same measurement the C++ fly uses). out[0]=dirX, out[1]=dirZ normalized,
// out[2]=residual magnitude. false = below deadzone / unavailable.
JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_getWorldInputDir(
    JNIEnv* env, jobject /*self*/, jfloatArray out) {
    using namespace eclient_runtime::kotlinbridge;
    constexpr float AIR_FRICTION = 0.98f;
    constexpr float DEADZONE = 0.004f;
    if (env->GetArrayLength(out) < 3) return JNI_FALSE;
    Vec3* vel = velocityPtr(g_player.load());
    if (!vel) return JNI_FALSE;
    if (!std::isfinite(vel->x) || !std::isfinite(vel->z)) return JNI_FALSE;
    float ax = vel->x - (g_havePrevOut ? AIR_FRICTION * g_prevOut.x : 0.0f);
    float az = vel->z - (g_havePrevOut ? AIR_FRICTION * g_prevOut.z : 0.0f);
    ax = std::clamp(ax, -0.35f, 0.35f);
    az = std::clamp(az, -0.35f, 0.35f);
    const float mag = std::sqrt(ax * ax + az * az);
    if (mag <= DEADZONE) return JNI_FALSE;
    const jfloat v[3] = {ax / mag, az / mag, mag};
    env->SetFloatArrayRegion(out, 0, 3, v);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_buttonUp(
    JNIEnv* /*env*/, jobject /*self*/) {
    return eclient_runtime::flyinput::heldUp() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_buttonDown(
    JNIEnv* /*env*/, jobject /*self*/) {
    return eclient_runtime::flyinput::heldDown() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL Java_com_rubidiumclient_modules_NativeBridge_getFloatSetting(
    JNIEnv* env, jobject /*self*/, jstring module, jstring key, jfloat fallback) {
    using eclient_runtime::modules::ModuleManager;
    const char* m = env->GetStringUTFChars(module, nullptr);
    const char* k = env->GetStringUTFChars(key, nullptr);
    const float v = ModuleManager::instance().floatSetting(m, k, fallback);
    env->ReleaseStringUTFChars(key, k);
    env->ReleaseStringUTFChars(module, m);
    return v;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_getBoolSetting(
    JNIEnv* env, jobject /*self*/, jstring module, jstring key, jboolean fallback) {
    using eclient_runtime::modules::ModuleManager;
    const char* m = env->GetStringUTFChars(module, nullptr);
    const char* k = env->GetStringUTFChars(key, nullptr);
    const bool v = ModuleManager::instance().boolSetting(m, k, fallback == JNI_TRUE);
    env->ReleaseStringUTFChars(key, k);
    env->ReleaseStringUTFChars(module, m);
    return v ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_rubidiumclient_modules_NativeBridge_logLine(
    JNIEnv* env, jobject /*self*/, jstring line) {
    const char* s = env->GetStringUTFChars(line, nullptr);
    __android_log_print(ANDROID_LOG_INFO, "EClientKotlin", "%s", s);
    env->ReleaseStringUTFChars(line, s);
}

// FullBright: a straight plumbing call to the (validated, memory-backed)
// patch profile. Called from the UI thread, not the tick.
JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_setFullBright(
    JNIEnv* /*env*/, jobject /*self*/, jboolean enabled) {
    return eclient_runtime::profile::PatchManager::instance()
                   .set("FullBright", enabled == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

// ════════════════════════════════════════════════════════════════════════════
// Native module surface for the Kotlin Views UI (r25)
//
// The panel fetches the C++ module list in ONE call per redraw instead of an
// N-call ping-pong. Line format, tab-separated:
//     name \t category \t enabled(0|1) \t memoryBacked(0|1) \t description
// The description is flattened to a single line (the UI has no multi-line rows).
// ════════════════════════════════════════════════════════════════════════════

JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeListModules(
    JNIEnv* env, jobject /*self*/) {
    using eclient_runtime::modules::ModuleManager;
    std::string out;
    for (const auto& m : ModuleManager::instance().all()) {
        out += m.name;
        out += '\t';
        out += m.category;
        out += '\t';
        out += m.enabled ? "1" : "0";
        out += '\t';
        out += m.memoryBacked ? "1" : "0";
        out += '\t';
        for (char c : m.description) out += (c == '\n' || c == '\t') ? ' ' : c;
        out += '\n';
    }
    return env->NewStringUTF(out.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeModuleEnabled(
    JNIEnv* env, jobject /*self*/, jstring name) {
    using eclient_runtime::modules::ModuleManager;
    if (!name) return JNI_FALSE;
    const char* n = env->GetStringUTFChars(name, nullptr);
    if (!n) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_FALSE;
    }
    const bool on = ModuleManager::instance().enabled(n);
    env->ReleaseStringUTFChars(name, n);
    return on ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeSetModuleEnabled(
    JNIEnv* env, jobject /*self*/, jstring name, jboolean enabled) {
    using eclient_runtime::modules::ModuleManager;
    if (!name) return JNI_FALSE;
    const char* n = env->GetStringUTFChars(name, nullptr);
    if (!n) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_FALSE;
    }
    // Returns false when the layer refuses — for a memory-backed module that
    // means the patch profile was not validated, and the UI must show that
    // instead of a switch that silently lies.
    const bool ok = ModuleManager::instance().setEnabled(n, enabled == JNI_TRUE);
    env->ReleaseStringUTFChars(name, n);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativePatchStatus(
    JNIEnv* env, jobject /*self*/) {
    using eclient_runtime::profile::PatchManager;
    auto& pm = PatchManager::instance();
    std::string s = pm.ready() ? "patches: VALIDATED" : "patches: NOT VALIDATED";
    if (!pm.ready()) {
        const std::string why = pm.status();
        if (!why.empty()) { s += " ("; s += why; s += ")"; }
    }
    return env->NewStringUTF(s.c_str());
}

JNIEXPORT void JNICALL Java_com_rubidiumclient_modules_NativeBridge_setKotlinTickActive(
    JNIEnv* /*env*/, jobject /*self*/, jboolean active) {
    eclient_runtime::kotlinbridge::g_kotlinTickActive.store(active == JNI_TRUE);
}

} // extern "C"
