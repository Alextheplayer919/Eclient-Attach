#pragma once
// Read-only diagnostics using the same entity/block wrappers Apollon already
// calls. Does not spawn, attack, switch inventory or place anything.
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include "block_names.h"
#include "base_search.h"
#include "placement_forward.h"
#include "focused_events.h"
namespace eclient_combat_diagnostics {
static std::atomic<bool> enabled{false};
static std::atomic<bool> focused{false};
static std::atomic<bool> swapTrace{false};
static std::atomic<bool> swapHookReady{false};
static std::atomic<bool> requestHookReady{false};
static std::atomic<unsigned> swapEvents{0};
static std::uintptr_t swapActionVtable = 0, takeActionVtable = 0, placeActionVtable = 0;
static std::atomic<unsigned> focusedSession{0};
static std::mutex logMutex;
static std::string path;
static void setLogPath(const char* value) { std::lock_guard<std::mutex> lock(logMutex); path = value; }
static void draw() {
    bool on = enabled.load();
    if (ImGui::Button(on ? "Stop diagnostics" : "Start diagnostics")) {
        if (!on) focused.store(false);
        enabled.store(!on);
    }
    ImGui::TextWrapped("Read-only test: manually place and break crystals and obsidian. This is not Crystal Aura or Fake Player.");
    ImGui::TextWrapped("Log: Downloads/combat-test.txt. Placement calls can include block coordinates; no player names are logged.");
}
// Manual markers are explicitly labelled: these are not detected inventory events.
static void marker(const char* label) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    std::fprintf(f, "time=%lld manual_marker=%s\n", (long long)std::time(nullptr), label);
    std::fclose(f);
}
static void drawFocused() {
    bool on = focused.load();
    if (ImGui::Button(on ? "Stop focused test" : "Start focused test")) {
        if (!on) {
            enabled.store(false);
            ++focusedSession;
            focused.store(true);
            marker("focused_test_started_inventory_not_automatically_observed");
        } else {
            focused.store(false);
            marker("focused_test_stopped");
        }
    }
    ImGui::TextWrapped("Focused log: placement calls and nearby crystal count changes only. A count drop is not proof of destruction.");
    ImGui::TextWrapped("Inventory and offhand changes are not detected yet. Optional buttons below add manual markers, not game events.");
    if (focused.load()) {
        if (ImGui::Button("Mark totem moved")) marker("user_reports_totem_moved");
        if (ImGui::Button("Mark obsidian moved")) marker("user_reports_obsidian_moved");
        if (ImGui::Button("Mark crystal moved")) marker("user_reports_crystal_moved");
    }
    ImGui::TextWrapped("Downloads/combat-test.txt; placement calls may contain world coordinates and can include other items.");
}
// Experimental passive trace. The first 13 bytes of each input are copied by
// the identified constructor. They are opaque; never reinterpret as a slot or
// retain pointers. process_vm_readv fails closed if either range is unreadable.
// Events may arise outside manual swaps and do not prove server acceptance.
struct SwapPrefix { unsigned char bytes[13]; };
static void swapConstruction(std::uintptr_t first, std::uintptr_t second) {
    if (!swapTrace.load() || panic) return;
    SwapPrefix a{}, b{};
    const bool aOk = eclient::fly2::probe::read(first, a);
    const bool bOk = eclient::fly2::probe::read(second, b);
    std::lock_guard<std::mutex> lock(logMutex);
    if (!swapTrace.load() || path.empty() || swapEvents.load() >= 100) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f = std::fopen(path.c_str(), "w"); if (!f) return; }
    unsigned count = ++swapEvents;
    std::fprintf(f, "time=%lld swap_constructor_event=%u first13=", (long long)std::time(nullptr), count);
    if (aOk) for (auto byte : a.bytes) std::fprintf(f, "%02x", (unsigned)byte); else std::fprintf(f, "unreadable");
    std::fprintf(f, " second13=");
    if (bOk) for (auto byte : b.bytes) std::fprintf(f, "%02x", (unsigned)byte); else std::fprintf(f, "unreadable");
    std::fprintf(f, " (opaque_inputs_not_proof_of_completed_swap)\n");
    std::fclose(f);
}
// The request function reads a pointer from its second argument, then a byte
// at action+8. Log that byte and bounded prefixes only for recognized
// transfer action vtables; the byte is a candidate action type, not proof of
// a move or server acknowledgment. Never retain or dereference the pointer
// outside this call, and avoid recording addresses or item contents.
static void requestAction(std::uintptr_t ownerPointer) {
    if (!swapTrace.load() || panic) return;
    std::uintptr_t action = 0;
    std::uint8_t type = 0;
    bool readable = eclient::fly2::probe::read(ownerPointer, action) && action <= UINTPTR_MAX - 0x45 &&
        eclient::fly2::probe::read(action + 8, type);
    std::uintptr_t vtable=0;
    bool known = readable && eclient::fly2::probe::read(action, vtable) && vtable &&
        (vtable == swapActionVtable || vtable == takeActionVtable || vtable == placeActionVtable);
    const char* kind = !known ? "other" : vtable == swapActionVtable ? "swap" : vtable == takeActionVtable ? "take" : "place";
    SwapPrefix a{}, b{};
    bool aOk=known && eclient::fly2::probe::read(action+0x10, a);
    bool bOk=known && eclient::fly2::probe::read(action+0x38, b);
    std::lock_guard<std::mutex> lock(logMutex);
    if (!swapTrace.load() || path.empty() || swapEvents.load() >= 100) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f=std::fopen(path.c_str(), "w"); if (!f) return; }
    unsigned count = ++swapEvents;
    std::fprintf(f, "time=%lld request_action_event=%u", (long long)std::time(nullptr), count);
    if (readable) std::fprintf(f, " candidate_type=%u", (unsigned)type);
    else std::fprintf(f, " candidate_type=unreadable");
    std::fprintf(f, " vtable_kind=%s", kind);
    if (known) {
        std::fprintf(f, " prefix_a=");
        if (aOk) for (auto byte : a.bytes) std::fprintf(f, "%02x", (unsigned)byte); else std::fprintf(f, "unreadable");
        std::fprintf(f, " prefix_b=");
        if (bOk) for (auto byte : b.bytes) std::fprintf(f, "%02x", (unsigned)byte); else std::fprintf(f, "unreadable");
    }
    std::fprintf(f, " (submitted_not_confirmation)\n");
    std::fclose(f);
}
static void drawSwapTrace() {
    ImGui::TextWrapped("Experimental passive hooks observe swap construction and submitted request actions. Installed at startup only on matching code bytes; they may crash the game. They do not move items or confirm success.");
    ImGui::TextWrapped("Writes candidate action types and opaque inventory-related bytes to Downloads/combat-test.txt; they may contain sensitive metadata. Up to 100 events per recording.");
    if (!swapHookReady.load()) ImGui::TextWrapped("Swap constructor hook unavailable on this build.");
    if (!requestHookReady.load()) ImGui::TextWrapped("Request action hook unavailable on this build.");
    if (!swapHookReady.load() && !requestHookReady.load()) return;
    const bool on=swapTrace.load();
    if (ImGui::Button(on ? "Stop swap trace" : "Start swap trace")) {
        if (on) { swapTrace.store(false); marker("swap_trace_stopped"); }
        else { swapEvents.store(0); marker("swap_trace_started"); swapTrace.store(true); }
    }
    if (on && ImGui::Button("Mark next manual swap")) marker("next_manual_inventory_swap");
}
// Match Apollon's Xray hook: BlockType+8 is a native std::string containing
// a tile description name. Validate both object and characters before use.
static std::string blockName(BlockType* type) {
    if (!type) return {};
    const auto address = reinterpret_cast<std::uintptr_t>(type) + 8;
    unsigned char header[sizeof(std::string)];
    iovec local{header, sizeof(header)}, remote{reinterpret_cast<void*>(address), sizeof(header)};
    if (syscall(__NR_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) != static_cast<ssize_t>(sizeof(header))) return {};
    const auto* value = reinterpret_cast<const std::string*>(address);
    const auto length = value->size();
    if (!length || length > 100) return {};
    const char* data = value->data();
    char name[101]{};
    iovec dst{name, length}, src{const_cast<char*>(data), length};
    if (syscall(__NR_process_vm_readv, getpid(), &dst, 1, &src, 1, 0) != static_cast<ssize_t>(length)) return {};
    for (size_t i=0; i<length; ++i) {
        char c=name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return {};
    }
    return std::string(name, length);
}
// Placement candidate was identified in the pinned binary. Log only while
// diagnostics are enabled; never change the arguments or call behavior.
static void placement(const eclient::combat::PlacementArgs& args, std::uint64_t result, bool repeated) {
    if (!enabled.load() && !focused.load()) return;
    std::lock_guard<std::mutex> lock(logMutex);
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f = std::fopen(path.c_str(), "w"); if (!f) return; }
    struct Int3 { int x,y,z; } pos{};
    struct Float3 { float x,y,z; } hit{};
    const bool positionRead = eclient::fly2::probe::read(args.a2, pos);
    const bool vectorRead = eclient::fly2::probe::read(args.a4, hit);
    // a2 is a position-like pointer in the disassembly; a4's role and the
    // rest of the signature remain candidates, not confirmed semantics.
    std::fprintf(f, "time=%lld placement_call arg3=%u arg6=%u result=%llu repeated=%d arg1_present=%d arg5_present=%d",
        (long long)std::time(nullptr), args.a3, args.a6, (unsigned long long)result, repeated ? 1 : 0,
        args.a1 != 0 ? 1 : 0, args.a5 != 0 ? 1 : 0);
    if (positionRead) std::fprintf(f," candidate_block=%d,%d,%d",pos.x,pos.y,pos.z);
    if (vectorRead && std::isfinite(hit.x) && std::isfinite(hit.y) && std::isfinite(hit.z))
        std::fprintf(f," candidate_vector=%.3f,%.3f,%.3f",hit.x,hit.y,hit.z);
    std::fprintf(f,"\n");
    std::fclose(f);
}
static void focusedTick(LocalPlayer* player, bool ready) {
    if (!focused.load() || !ready || !player || panic || inMenu) return;
    static unsigned session = 0;
    static eclient::combat::CrystalCountEvents events;
    static std::chrono::steady_clock::time_point last{};
    const unsigned current = focusedSession.load();
    if (session != current) { session = current; events.reset(); last = {}; }
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(250)) return;
    last = now;
    Level* level = player->getLevel();
    vec3_t* p = player->getPosition();
    if (!level || !p || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z)) return;
    int count = 0;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || actor == player || actor->getEntityTypeId() != 71) continue;
        vec3_t* pos = actor->getPosition();
        if (!pos || !std::isfinite(pos->x) || !std::isfinite(pos->y) || !std::isfinite(pos->z)) continue;
        const float x=pos->x-p->x, y=pos->y-p->y, z=pos->z-p->z;
        if (x*x+y*y+z*z <= 12.0f*12.0f) ++count;
    }
    if (!events.update(count)) return;
    std::lock_guard<std::mutex> lock(logMutex);
    if (path.empty() || !focused.load() || focusedSession.load() != current) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f = std::fopen(path.c_str(), "w"); if (!f) return; }
    std::fprintf(f, "time=%lld nearby_crystal_count=%d radius=12 (observation_not_destruction_proof)\n", (long long)std::time(nullptr), count);
    std::fclose(f);
}
static void tick(LocalPlayer* player, bool ready) {
    focusedTick(player, ready);
    if (!enabled.load() || !ready || !player || panic || inMenu) return;
    static std::time_t last = 0;
    const auto now = std::time(nullptr);
    if (now == last) return;
    last = now;
    std::lock_guard<std::mutex> lock(logMutex);
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    if (std::ftell(f) > 1024*1024) { std::fclose(f); f = std::fopen(path.c_str(), "w"); if (!f) return; }
    Level* level = player->getLevel();
    vec3_t* p = player->getPosition();
    if (!level || !p || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z) ||
        std::fabs(p->x) > 30000000 || std::fabs(p->z) > 30000000 || std::fabs(p->y) > 4096) {
        std::fprintf(f, "time=%lld snapshot_unavailable\n", (long long)now); std::fclose(f); return;
    }
    std::fprintf(f, "time=%lld snapshot begin (relative to player, no names)\n", (long long)now);
    auto actors = level->getRuntimeActorList();
    unsigned count = 0;
    for (auto* actor : actors) {
        if (!actor || actor == player) continue;
        vec3_t* pos = actor->getPosition();
        if (!pos || !std::isfinite(pos->x) || !std::isfinite(pos->y) || !std::isfinite(pos->z)) continue;
        const float x=pos->x-p->x, y=pos->y-p->y, z=pos->z-p->z;
        if (x*x+y*y+z*z > 12.0f*12.0f) continue;
        if (++count > 64) break;
        const int id = actor->getEntityTypeId();
        std::fprintf(f," actor type=%d kind=%s relative=%.2f,%.2f,%.2f\n", id, id == 71 ? "ender_crystal" : "other", x,y,z);
    }
    std::fprintf(f," nearby_actor_count%s=%u\n", count > 64 ? "_at_least" : "", count);
    BlockSource* blocks = player->getDimensionBlockSource();
    static std::unordered_map<BlockType*, unsigned> blockTypes;
    if (blockTypes.size() > 512) blockTypes.clear();
    if (blocks) {
        const int px = static_cast<int>(std::floor(p->x)), py = static_cast<int>(std::floor(p->y-1.62f)), pz = static_cast<int>(std::floor(p->z));
        for (int x=-1; x<=1; ++x) for (int z=-1; z<=1; ++z) for (int y=-1; y<=1; ++y) {
            vec3_ti coords{px+x,py+y,pz+z};
            Block* b = blocks->getBlock(coords);
            if (!b) { std::fprintf(f," block offset=%d,%d,%d unavailable\n",x,y,z); continue; }
            BlockType* type = b->getBlockTypeClassCustom();
            unsigned token = 0;
            if (type) {
                auto entry = blockTypes.find(type);
                if (entry == blockTypes.end()) entry = blockTypes.emplace(type, static_cast<unsigned>(blockTypes.size())+1).first;
                token = entry->second;
            }
            const auto name = blockName(type);
            const auto known = eclient::combat::classifyBlockName(name);
            std::fprintf(f," block offset=%d,%d,%d air=%d type_token=%u name=%s kind=%s\n",x,y,z, b->isAir() ? 1 : 0, token,
                name.empty() ? "unavailable" : name.c_str(), eclient::combat::blockLabel(known));
        }
        // Read-only search across a normal nearby reach rather than only the
        // player's feet. It does not target a player, check collisions or act.
        const auto readBlock = [&](eclient::combat::Pos pos) {
            vec3_ti coords{pos.x,pos.y,pos.z};
            Block* b = blocks->getBlock(coords);
            if (!b) return eclient::combat::Block::unknown;
            if (b->isAir()) return eclient::combat::Block::air;
            auto kind = eclient::combat::classifyBlockName(blockName(b->getBlockTypeClassCustom()));
            if (kind == eclient::combat::KnownBlock::obsidian) return eclient::combat::Block::obsidian;
            if (kind == eclient::combat::KnownBlock::bedrock) return eclient::combat::Block::bedrock;
            return eclient::combat::Block::unknown;
        };
        const auto nearby = eclient::combat::findNearbyBase({p->x,p->y,p->z}, py, 4.5f, readBlock);
        if (nearby.found) std::fprintf(f," nearby_base offset=%d,%d,%d reach=%.2f clear_above=yes\n",
            nearby.base.x-px, nearby.base.y-py, nearby.base.z-pz, std::sqrt(nearby.distanceSquared));
        else std::fprintf(f," nearby_base none_with_clear_air_in_range\n");
    } else std::fprintf(f," block_source_unavailable\n");
    std::fprintf(f,"snapshot end (placement, inventory, fake player and totem unavailable)\n");
    std::fclose(f);
}
}
