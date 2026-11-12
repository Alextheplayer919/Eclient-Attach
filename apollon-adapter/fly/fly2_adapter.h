#pragma once
// Included after Apollon's game wrappers, variables and ImGui declarations.
#include <atomic>
#include <cstring>
#include "fly2_limits.h"
#ifndef ECLIENT_FLY2_TEST
#include "fly2_probe.h"
#include <mutex>
#include <string>
#include <cstdio>
#include <time.h>
#endif

namespace eclient_fly2_adapter {
// UI thread writes the desired state, game thread alone changes abilities.
static std::atomic<bool> enabled{false};
static std::atomic<float> horizontalBps{4.0f}, upBps{4.0f}, downBps{4.0f};
#ifndef ECLIENT_FLY2_TEST
static std::atomic<bool> calculated{true};
static std::atomic<float> glide{-0.02f}, upFactor{0.55f}, downFactor{0.55f};
static eclient::fly2::CalculationState calculation;
static std::mutex logMutex;
static std::string logPath;
static void setLogPath(const char* path) { std::lock_guard<std::mutex> guard(logMutex); logPath = path; }
static void logSample(const char* reason, const eclient::fly2::probe::Raw* r = nullptr, int status = -1, const eclient::fly2::Vec3* velocity = nullptr) {
    static time_t last = 0;
    time_t now = time(nullptr);
    if (now == last) return;
    last = now;
    std::lock_guard<std::mutex> guard(logMutex);
    if (logPath.empty()) return;
    FILE* f = std::fopen(logPath.c_str(), "a");
    if (!f) return;
    // Bound the file for repeated test sessions; it contains no coordinates.
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f = std::fopen(logPath.c_str(), "w"); if (!f) return; }
    std::fprintf(f, "time=%lld %s status=%d", (long long)now, reason, status);
    if (r) std::fprintf(f, " motion=%.3f,%.3f raw=%.3f,%.3f analog=%.3f,%.3f jump=%u sneak=%u yaw=%.2f",
        r->snapshot.input.strafe, r->snapshot.input.forward, r->rawX, r->rawY, r->analogX, r->analogY,
        r->jump, r->sneak, r->snapshot.input.yawDegrees);
    if (velocity) std::fprintf(f, " output=%.3f,%.3f,%.3f", velocity->x, velocity->y, velocity->z);
    std::fputc('\n', f); std::fclose(f);
}
#endif
static LocalPlayer* appliedPlayer = nullptr;
static bool applied = false;

static void draw() {
    bool on = enabled.load(std::memory_order_relaxed);
    if (ImGui::Button(on ? "Disable##fly2" : "Enable##fly2")) {
        on = !on;
        enabled.store(on, std::memory_order_release);
    }
    ImGui::SameLine(); ImGui::TextUnformatted(on ? "Enabled" : "Disabled");
#ifndef ECLIENT_FLY2_TEST
    bool mode = calculated.load();
    if (ImGui::Checkbox("Experimental calculated mode", &mode)) calculated.store(mode);
    ImGui::TextWrapped("Experimental input offsets: test in a safe local world first. Uncheck for previous ability flight.");
    if (mode) {
        auto setting = [](const char* label, std::atomic<float>& value, float lo, float hi) {
            float v = value.load(); if (ImGui::SliderFloat(label, &v, lo, hi, "%.2f")) value.store(v);
        };
        setting("Glide##fly2", glide, -0.3f, 0.0f);
        setting("Up H factor##fly2", upFactor, 0.1f, 1.0f);
        setting("Down H factor##fly2", downFactor, 0.1f, 1.0f);
    }
#endif
    ImGui::TextWrapped("Calculated mode uses the speed settings as targets; fallback uses them as limits. Servers may reject flight.");
    ImGui::TextWrapped("Speed limiting is unavailable with Anti Knockback enabled.");
    auto slider = [](const char* label, std::atomic<float>& setting, float maximum) {
        float value = setting.load(std::memory_order_relaxed);
        if (ImGui::SliderFloat(label, &value, 1.0f, maximum, "%.1f BPS")) setting.store(value, std::memory_order_relaxed);
    };
    slider("Horizontal limit##fly2", horizontalBps, 100.0f);
    slider("Up limit##fly2", upBps, 60.0f);
    slider("Down limit##fly2", downBps, 60.0f);
}

static void tick(LocalPlayer* player, bool gameReady) {
    if (!player) {
#ifndef ECLIENT_FLY2_TEST
        eclient::fly2::reset(calculation);
#endif
        appliedPlayer = nullptr; applied = false; return;
    }
    if (player != appliedPlayer) {
#ifndef ECLIENT_FLY2_TEST
        eclient::fly2::reset(calculation);
#endif
        appliedPlayer = player; applied = false;
    }
    const bool active = enabled.load(std::memory_order_acquire) && gameReady && !panic && !inMenu;
#ifndef ECLIENT_FLY2_TEST
    if (!active || !calculated.load()) eclient::fly2::reset(calculation);
#endif
    if (!active && !applied) return;
    Abilities* abilities = player->getAbilities();
    LayeredAbilities* layer = abilities ? abilities->getLayer(2) : nullptr;
    if (!layer) return;
    if (active) {
        // Same engine ability calls as Apollon's own Creative/Force Fly. The
        // game handles its normal joystick, jump/sneak controls and hover.
        layer->setAbility(10, true); // may fly
        layer->setAbility(9, true);  // currently flying
        applied = true;
        // In the pinned ARM64 profile the position pointer is the beginning of
        // the state vector; velocity is +0x18. The validated lerpMotion entry
        // copies the vector there. Never write if Anti Knockback patched it.
#ifndef ECLIENT_FLY2_TEST
        const auto address = getAbsoluteAddress(MineLib, Offsets64::LerpMotion);
        const unsigned char original[] = {0x08, 0x04, 0x41, 0xf9};
        vec3_t* position = player->getPosition();
        if (calculated.load()) {
            eclient::fly2::probe::Raw raw;
            if (antiKnockback || !address || !position || std::memcmp((const void*)address, original, sizeof(original)) != 0) {
                eclient::fly2::reset(calculation); logSample("motion_unavailable");
            } else if (!eclient::fly2::probe::sample(reinterpret_cast<std::uintptr_t>(player), raw)) {
                eclient::fly2::reset(calculation); logSample("input_unavailable");
            } else {
                const eclient::fly2::Settings settings{horizontalBps.load(), upBps.load(), downBps.load(), glide.load(), upFactor.load(), downFactor.load()};
                eclient::fly2::Vec3 result{};
                const auto status = eclient::fly2::calculate(calculation, true, true, raw.snapshot, settings, result);
                logSample("calculated", &raw, static_cast<int>(status), status == eclient::fly2::CalculationStatus::motion ? &result : nullptr);
                if (status == eclient::fly2::CalculationStatus::motion) {
                    vec3_t velocity(result.x, result.y, result.z);
                    using Lerp = void (*)(LocalPlayer*, const vec3_t*);
                    reinterpret_cast<Lerp>(address)(player, &velocity);
                }
            }
        } else if (!antiKnockback && address && position && std::memcmp((const void*)address, original, sizeof(original)) == 0) {
            vec3_t velocity = *reinterpret_cast<const vec3_t*>(reinterpret_cast<const char*>(position) + 0x18);
            const eclient::fly2::Limits limits{horizontalBps.load(), upBps.load(), downBps.load()};
            if (eclient::fly2::capVelocity(velocity.x, velocity.y, velocity.z, limits)) {
                using Lerp = void (*)(LocalPlayer*, const vec3_t*);
                reinterpret_cast<Lerp>(address)(player, &velocity);
            }
        }
#endif
    } else {
        // Follow Apollon's survival-only cleanup policy. Don't cancel another
        // enabled flight module; never dereference a previous player pointer.
        if (player->isAttackableGamemode()) {
            if (!forceCreativeFly) layer->setAbility(9, false);
            if (!creativeFly) layer->setAbility(10, false);
        }
        applied = false;
    }
}
}
