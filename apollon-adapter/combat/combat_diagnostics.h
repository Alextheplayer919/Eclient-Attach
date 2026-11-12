#pragma once
// Read-only diagnostics using the same entity/block wrappers Apollon already
// calls. Does not spawn, attack, switch inventory or place anything.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include "block_names.h"
#include "base_search.h"
namespace eclient_combat_diagnostics {
static std::atomic<bool> enabled{false};
static std::mutex logMutex;
static std::string path;
static void setLogPath(const char* value) { std::lock_guard<std::mutex> lock(logMutex); path = value; }
static void draw() {
    bool on = enabled.load();
    if (ImGui::Button(on ? "Stop diagnostics" : "Start diagnostics")) enabled.store(!on);
    ImGui::TextWrapped("Read-only test: manually place and break crystals and obsidian. This is not Crystal Aura or Fake Player.");
    ImGui::TextWrapped("Log: Downloads/combat-test.txt. It includes nearby entity types, relative locations and block observations, not player names.");
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
static void tick(LocalPlayer* player, bool ready) {
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
