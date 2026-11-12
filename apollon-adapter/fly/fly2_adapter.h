#pragma once
// Included after Apollon's game wrappers, variables and ImGui declarations.
#include "fly2.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

namespace eclient_fly2_adapter {
using Clock = std::chrono::steady_clock;
static std::atomic<bool> enabled{false};
static std::atomic<float> horizontal{46.0f}, up{19.8f}, down{46.0f}, glide{-0.02f}, upFactor{0.55f}, downFactor{0.55f};
static std::atomic<unsigned> command{0};
static std::atomic<long long> commandTime{0};
static unsigned frameCommand = 0; // render thread only
static eclient::fly2::State state; // game thread only
static LocalPlayer* lastPlayer = nullptr;
static long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
static void beginFrame() { frameCommand = 0; }
static void finishFrame() {
    // Publish the timestamp before the command: a stale command must never
    // survive a paused/destroyed renderer.
    commandTime.store(nowNs(), std::memory_order_release);
    command.store(frameCommand, std::memory_order_release);
}
static void held(const char* label, unsigned bit) {
    ImGui::Button(label);
    if (ImGui::IsItemActive()) frameCommand |= bit;
}
static void slider(const char* label, std::atomic<float>& value, float min, float max, const char* format) {
    float v = value.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(label, &v, min, max, format)) value.store(v, std::memory_order_relaxed);
}
static void draw() {
    bool on = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Fly2 (experimental)", &on)) enabled.store(on, std::memory_order_release);
    if (!on) return;
    ImGui::TextWrapped("Hold a Fly2 control to move. Release to stop writes. Anti Knockback must be off.");
    held("Forward##Fly2", 1); ImGui::SameLine(); held("Back##Fly2", 2);
    held("Left##Fly2", 4); ImGui::SameLine(); held("Right##Fly2", 8);
    held("Up##Fly2", 16); ImGui::SameLine(); held("Down##Fly2", 32); ImGui::SameLine(); held("Glide##Fly2", 64);
    slider("H Speed BPS##Fly2", horizontal, 1, 100, "%.1f");
    slider("Up Speed BPS##Fly2", up, 1, 60, "%.1f");
    slider("Down Speed BPS##Fly2", down, 1, 60, "%.1f");
    slider("Glide##Fly2", glide, -0.3f, 0, "%.2f");
    slider("Up H Factor##Fly2", upFactor, 0.1f, 1, "%.2f");
    slider("Down H Factor##Fly2", downFactor, 0.1f, 1, "%.2f");
}
static void tick(LocalPlayer* player) {
    const unsigned bits = command.load(std::memory_order_acquire);
    const long long age = nowNs() - commandTime.load(std::memory_order_acquire);
    if (!player || !enabled.load(std::memory_order_acquire) || !bits || age < 0 || age > 250000000LL ||
        panic || inMenu || !inHudScreen || antiKnockback) {
        eclient::fly2::reset(state); lastPlayer = nullptr; return;
    }
    if (player != lastPlayer) { eclient::fly2::reset(state); lastPlayer = player; }
    // Anti Knockback patches this exact method. Do not call if it or another
    // patch has changed the entry; never bypass an active patch.
    const auto addr = getAbsoluteAddress(MineLib, Offsets64::LerpMotion);
    const unsigned char original[] = {0x08,0x04,0x41,0xf9};
    if (!addr || std::memcmp((const void*)addr, original, sizeof(original)) != 0) {
        eclient::fly2::reset(state); return;
    }
    vec3_t* pos = player->getPosition();
    vec2_t* rot = player->getRotation();
    if (!pos || !rot) { eclient::fly2::reset(state); return; }
    const eclient::fly2::Input input{{pos->x,pos->y,pos->z},
        float(bool(bits & 8)) - float(bool(bits & 4)),
        float(bool(bits & 1)) - float(bool(bits & 2)), rot->y,
        bool(bits & 16), bool(bits & 32)};
    const eclient::fly2::Settings settings{horizontal.load(), up.load(), down.load(), glide.load(), upFactor.load(), downFactor.load()};
    eclient::fly2::Vec3 result{};
    if (eclient::fly2::plan(state, input, settings, result) != eclient::fly2::Result::motion) return;
    vec3_t velocity(result.x, result.y, result.z);
    using LerpMotion = void (*)(LocalPlayer*, const vec3_t*);
    reinterpret_cast<LerpMotion>(addr)(player, &velocity);
}
}
