#include "kotlin/KotlinBridge.h"

#include <cstdio>

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "memory/ComponentWalk.h"
#include "memory/NativeMemory.h"
#include "modules/FlyInput.h"
#include "modules/MelodyModules.h"
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
// Native module SETTINGS for the in-game panel (r42)
//
// The in-game GUI is the only GUI that can drive memory-backed modules, so it
// has to be able to configure them, not just toggle them. One call per module
// returns every setting; one call applies a value.
// ════════════════════════════════════════════════════════════════════════════

namespace {

// Flatten a label/tooltip for a single-line tab-separated wire format.
std::string flatten(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) out += (c == '\n' || c == '\t' || c == '\r') ? ' ' : c;
    return out;
}

const eclient_runtime::modules::Setting* findSettingByName(const std::string& module,
                                                          const std::string& key) {
    using eclient_runtime::modules::ModuleManager;
    for (const auto& m : ModuleManager::instance().all()) {
        if (m.name != module) continue;
        for (const auto& s : m.settings) {
            if (s.key == key) return &s;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeListSettings(
    JNIEnv* env, jobject /*self*/, jstring module) {
    using eclient_runtime::modules::ModuleManager;
    std::string out;
    if (module) {
        const char* want = env->GetStringUTFChars(module, nullptr);
        if (want) {
            for (const auto& m : ModuleManager::instance().all()) {
                if (m.name != want) continue;
                char buf[64];
                for (const auto& s : m.settings) {
                    const char kind =
                        s.type == eclient_runtime::modules::Setting::Type::Bool    ? 'b' :
                        s.type == eclient_runtime::modules::Setting::Type::Int     ? 'i' :
                        s.type == eclient_runtime::modules::Setting::Type::Float   ? 'f' : 'e';

                    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(s.floatValue));
                    const std::string value =
                        (kind == 'b') ? (s.boolValue ? "1" : "0") :
                        (kind == 'f') ? std::string(buf) :
                                        std::to_string(kind == 'e' ? s.enumValue : s.intValue);

                    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(s.floatMin));
                    const std::string fmin = buf;
                    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(s.floatMax));
                    const std::string fmax = buf;

                    const std::string smin = (kind == 'f') ? fmin : std::to_string(s.intMin);
                    const std::string smax = (kind == 'f') ? fmax : std::to_string(s.intMax);

                    std::string options;
                    if (kind == 'e') {
                        for (std::size_t i = 0; i < s.options.size(); ++i) {
                            if (i) options += ',';
                            options += flatten(s.options[i]);
                        }
                    }

                    // label falls back to key so a row can never render blank
                    out += s.key;
                    out += '\t' + flatten(s.label.empty() ? s.key : s.label);
                    out += '\t' + std::string(1, kind);
                    out += '\t' + value;
                    out += '\t' + smin;
                    out += '\t' + smax;
                    out += '\t' + options;
                    out += '\t' + flatten(s.tooltip);
                    out += '\n';
                }
                break;
            }
            env->ReleaseStringUTFChars(module, want);
        }
    }
    return env->NewStringUTF(out.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeSetSetting(
    JNIEnv* env, jobject /*self*/, jstring module, jstring key,
    jfloat f, jboolean b, jint i) {
    using eclient_runtime::modules::ModuleManager;
    if (!module || !key) return JNI_FALSE;
    const char* m = env->GetStringUTFChars(module, nullptr);
    const char* k = env->GetStringUTFChars(key, nullptr);
    if (!m || !k) {
        if (m) env->ReleaseStringUTFChars(module, m);
        if (k) env->ReleaseStringUTFChars(key, k);
        return JNI_FALSE;
    }

    // The setting's own type picks the field: the caller sends all three and
    // cannot desync from the native type.
    bool ok = false;
    const auto* s = findSettingByName(m, k);
    if (s) {
        using T = eclient_runtime::modules::Setting::Type;
        switch (s->type) {
            case T::Bool:  ok = ModuleManager::instance().setBoolSetting(m, k, b == JNI_TRUE); break;
            case T::Float: ok = ModuleManager::instance().setFloatSetting(m, k, static_cast<float>(f)); break;
            case T::Int:   ok = ModuleManager::instance().setIntSetting(m, k, static_cast<int>(i)); break;
            case T::Enum:  ok = ModuleManager::instance().setEnumSetting(m, k, static_cast<int>(i)); break;
        }
    }

    env->ReleaseStringUTFChars(key, k);
    env->ReleaseStringUTFChars(module, m);
    return ok ? JNI_TRUE : JNI_FALSE;
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

// Returns "" when the toggle applied, or the reason it did not.
//
// It used to return a bare bool, and the UI turned that into one fixed sentence
// ("patch profile not validated") no matter what had actually happened. That is
// why the panel could show "patches validated" and refuse every toggle in the
// same breath: the refusal was real, the explanation was invented. The reason
// now travels with the answer.
JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeSetModuleEnabled(
    JNIEnv* env, jobject /*self*/, jstring name, jboolean enabled) {
    using eclient_runtime::modules::ModuleManager;
    if (!name) return env->NewStringUTF("no module name");
    const char* n = env->GetStringUTFChars(name, nullptr);
    if (!n) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return env->NewStringUTF("module name was not readable");
    }
    std::string why;
    const bool ok = ModuleManager::instance().setEnabled(n, enabled == JNI_TRUE, &why);
    env->ReleaseStringUTFChars(name, n);
    return env->NewStringUTF(ok ? "" : why.c_str());
}

// The panel's diagnostic line. Read-only, and safe to call before the module
// layer is up — it then reports the stage that is missing.
JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_nativeDiag(
    JNIEnv* env, jobject /*self*/) {
    const std::string d = eclient_runtime::modules::melody::diagLine();
    return env->NewStringUTF(d.c_str());
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

// ════════════════════════════════════════════════════════════════════════════
// PVP bot data plane (r27)
//
// The bot's decisions are Kotlin (PvpBot.kt); these are the four questions a
// decision needs answered, plus the movement write. Everything goes through
// the same validated offsets and the same actor filter as KillAura2 (see
// melody::botScan), so the bot can never see an actor the module would refuse
// to attack.
//
// botScan layout — 11 floats per actor, nearest first:
//   [0..2] position  [3..5] velocity  [6] distance
//   [7] health       [8] type id      [9] flags (1 = damaged within 10 ticks)
//   [10] entity id, exact, carried in the low bits of a float (Kotlin reads it
//        with Float.fromBits). Identity matters: scan indexes reshuffle every
//        tick, so hit attribution has to key on something stable.
// Returns the actor count, or -1 when the world is not readable (fail inert).
// ════════════════════════════════════════════════════════════════════════════

namespace {
constexpr int kBotFloatsPerActor = 11;
constexpr int kBotMaxActors = 24; // JNI-side cap; the Kotlin side caps again
} // namespace

JNIEXPORT jint JNICALL Java_com_rubidiumclient_modules_NativeBridge_botScan(
    JNIEnv* env, jobject /*self*/, jfloatArray out, jfloat maxRange, jint mode) {
    using namespace eclient_runtime::modules::melody;
    if (!out || env->GetArrayLength(out) < kBotFloatsPerActor) return -1;

    const int cap = std::min(kBotMaxActors,
                             env->GetArrayLength(out) / kBotFloatsPerActor);
    std::vector<BotActor> actors(static_cast<std::size_t>(cap));
    const int n = botScan(eclient_runtime::kotlinbridge::g_player.load(),
                          static_cast<float>(maxRange), static_cast<int>(mode),
                          actors.data(), cap);
    if (n <= 0) return n;

    std::vector<jfloat> packed(static_cast<std::size_t>(n) * kBotFloatsPerActor);
    for (int i = 0; i < n; ++i) {
        const BotActor& a = actors[static_cast<std::size_t>(i)];
        jfloat* p = &packed[static_cast<std::size_t>(i) * kBotFloatsPerActor];
        p[0] = a.x; p[1] = a.y; p[2] = a.z;
        p[3] = a.vx; p[4] = a.vy; p[5] = a.vz;
        p[6] = a.distance;
        p[7] = static_cast<jfloat>(a.health);
        p[8] = static_cast<jfloat>(a.typeId);
        p[9] = static_cast<jfloat>(a.flags);
        // Entity id travels bit-exact through a float; see the layout comment.
        std::uint32_t idBits = a.entityId;
        jfloat idFloat = 0.0f;
        static_assert(sizeof(idBits) == sizeof(idFloat), "id must fit a float slot");
        std::memcpy(&idFloat, &idBits, sizeof(idFloat));
        p[10] = idFloat;
    }
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(packed.size()), packed.data());
    return n;
}

// One line describing the last scan's funnel: how many actors existed, how many
// were attackable, which stage rejected the rest, and which real type ids were
// seen. "The bot detects nobody" is never again a guess.
JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_botScanDiag(
    JNIEnv* env, jobject /*self*/) {
    const std::string s = eclient_runtime::modules::melody::botScanDiag();
    return env->NewStringUTF(s.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_botAttack(
    JNIEnv* /*env*/, jobject /*self*/, jint index) {
    using namespace eclient_runtime::modules::melody;
    const bool ok = botAttackIndex(eclient_runtime::kotlinbridge::g_player.load(),
                                   static_cast<int>(index));
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_botAim(
    JNIEnv* env, jobject /*self*/, jint index, jfloatArray out) {
    using namespace eclient_runtime::modules::melody;
    if (!out || env->GetArrayLength(out) < 2) return JNI_FALSE;
    float yawPitch[2]{};
    if (!botAimAt(eclient_runtime::kotlinbridge::g_player.load(),
                  static_cast<int>(index), yawPitch)) {
        return JNI_FALSE;
    }
    env->SetFloatArrayRegion(out, 0, 2, yawPitch);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_botRotationAvailable(
    JNIEnv* /*env*/, jobject /*self*/) {
    return eclient_runtime::modules::melody::botRotationAvailable() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_botSelf(
    JNIEnv* env, jobject /*self*/, jfloatArray out) {
    using namespace eclient_runtime::modules::melody;
    if (!out || env->GetArrayLength(out) < 10) return JNI_FALSE;
    float self[10]{};
    if (!botSelf(eclient_runtime::kotlinbridge::g_player.load(), self)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, 10, self);
    return JNI_TRUE;
}

// ════════════════════════════════════════════════════════════════════════════
// WALKING (r28) — the game's own movement, not a velocity hack.
//
// The bot writes the SAME input state the on-screen joystick writes and lets
// the engine do everything else: speed, friction, collision, sprint rules,
// animations, footsteps, knockback. No motion is synthesised.
//
// Why the previous version was wrong (deleted on purpose): writing the state
// vector's velocity is the FLY mechanism — it fights the engine's own
// acceleration every tick, produces no walk animation, and cannot respect the
// engine's rules (water, ice, ladders, sneaking). "Walk like a player" is both
// simpler and strictly more capable.
//
// Requires two derived facts, and is INERT without both:
//   1. the MoveInputComponent element layout (MoveInputVecOffset ...), and
//   2. the player's current yaw (ActorRotation* layout), because joystick input
//      is camera-relative while the bot plans in target-relative terms.
// botWalkStatus() reports exactly which one is missing, in the UI.
// ════════════════════════════════════════════════════════════════════════════

namespace {

// Resolve the local player's MoveInputComponent element. Read-only and guarded:
// used both by the writer and by the status reporter, so a wrong guess shows up
// as "pool not found" instead of a crash.
bool moveInputElement(std::uintptr_t& outElement, std::string& why) {
    using namespace eclient_runtime::profile::mc_1_21_111;
    void* player = eclient_runtime::kotlinbridge::g_player.load();
    if (!player) { why = "no tick context"; return false; }
    if (MoveInputComponentHash == 0) { why = "component hash unknown"; return false; }

    const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(player);
    std::uintptr_t registry = 0;
    if (!eclient_runtime::memory::NativeMemory::instance().read(
            p + EcsRegistryField, &registry, sizeof(registry)) || !registry) {
        why = "no ECS registry";
        return false;
    }
    std::uintptr_t pool = 0;
    for (const auto& d : eclient_runtime::ecs::listPools(p)) {
        if (d.typeHash == MoveInputComponentHash) { pool = d.pool; break; }
    }
    if (!pool) { why = "pool not in registry"; return false; }

    std::uint32_t eid = 0;
    if (!eclient_runtime::memory::NativeMemory::instance().read(
            p + EntityIdField, &eid, sizeof(eid))) {
        why = "no entity id";
        return false;
    }
    const auto el = eclient_runtime::ecs::resolveElement(pool, eid);
    if (!el.valid) { why = "element not resolved"; return false; }
    outElement = el.block + el.inBlock * MoveInputElementStride;
    return true;
}

} // namespace

// Short, honest status for the UI: what walking is waiting for.
JNIEXPORT jstring JNICALL Java_com_rubidiumclient_modules_NativeBridge_botWalkStatus(
    JNIEnv* env, jobject /*self*/) {
    using namespace eclient_runtime::profile::mc_1_21_111;
    void* player = eclient_runtime::kotlinbridge::g_player.load();
    if (!player) return env->NewStringUTF("walk: idle (no tick)");

    if (MoveInputVecOffset == 0) {
        // Pool is findable (hash known); the element LAYOUT is the missing piece.
        std::uintptr_t element = 0; std::string why;
        if (moveInputElement(element, why)) {
            return env->NewStringUTF("walk: pool OK, element layout not derived");
        }
        return env->NewStringUTF(("walk: input pool unavailable (" + why + ")").c_str());
    }
    if (ActorRotationStride == 0 || ActorRotationYawOffset == 0) {
        return env->NewStringUTF("walk: needs player yaw (rotation layout not derived)");
    }
    return env->NewStringUTF("walk: ready");
}

// Walk using the engine's own movement. dirX/dirZ are WORLD space; they are
// rotated into camera-local input axes with the player's current yaw, because
// that is the frame the joystick state lives in.
JNIEXPORT jboolean JNICALL Java_com_rubidiumclient_modules_NativeBridge_botWalk(
    JNIEnv* /*env*/, jobject /*self*/, jfloat dirX, jfloat dirZ, jboolean jump, jboolean sneak) {
    using namespace eclient_runtime::profile::mc_1_21_111;
    if (!std::isfinite(dirX) || !std::isfinite(dirZ)) return JNI_FALSE;
    if (MoveInputVecOffset == 0 || MoveInputForwardSign == 0 || MoveInputStrafeSign == 0) {
        return JNI_FALSE; // layout not derived — fail inert, never guess
    }

    std::uintptr_t element = 0; std::string why;
    if (!moveInputElement(element, why)) return JNI_FALSE;

    // The yaw the input is relative to. Without it the local axes are unknown,
    // so we refuse rather than walk in a random direction.
    const float yawDeg =
        eclient_runtime::modules::melody::botPlayerYaw(eclient_runtime::kotlinbridge::g_player.load());
    if (!std::isfinite(yawDeg)) return JNI_FALSE;

    const float yaw = yawDeg * 3.14159265358979323846f / 180.0f;
    // Bedrock yaw convention (matches getRotations: yaw = atan2(dz,dx)*RAD2DEG - 90)
    //   forward = (-sin yaw, cos yaw)      right = (-forwardZ, forwardX)
    const float fx = -std::sin(yaw), fz = std::cos(yaw);
    const float rx = -fz, rz = fx;

    float forward = dirX * fx + dirZ * fz;
    float strafe = dirX * rx + dirZ * rz;
    const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (len > 1.0f) { forward /= len; strafe /= len; } // joystick is a unit vector

    auto* vec = reinterpret_cast<float*>(element + MoveInputVecOffset);
    vec[0] = (MoveInputForwardSign < 0 ? -forward : forward);
    vec[1] = (MoveInputStrafeSign < 0 ? -strafe : strafe);

    if (MoveInputFlagOffset != 0) {
        auto* flags = reinterpret_cast<std::uint32_t*>(element + MoveInputFlagOffset);
        std::uint32_t v = *flags;
        const std::uint32_t mask = MoveInputJumpBit | MoveInputSneakBit | MoveInputSprintBit;
        v &= ~mask;
        if (jump == JNI_TRUE && MoveInputJumpBit) v |= MoveInputJumpBit;
        if (sneak == JNI_TRUE && MoveInputSneakBit) v |= MoveInputSneakBit;
        if (MoveInputSprintBit) v |= MoveInputSprintBit; // walking forward = sprint
        *flags = v;
    }
    return JNI_TRUE;
}
} // extern "C"
