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
        std::fprintf(f," actor type=%d relative=%.2f,%.2f,%.2f\n", actor->getEntityTypeId(), x,y,z);
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
            std::fprintf(f," block offset=%d,%d,%d air=%d type_token=%u\n",x,y,z, b->isAir() ? 1 : 0, token);
        }
    } else std::fprintf(f," block_source_unavailable\n");
    std::fprintf(f,"snapshot end (placement, inventory, fake player and totem unavailable)\n");
    std::fclose(f);
}
}
