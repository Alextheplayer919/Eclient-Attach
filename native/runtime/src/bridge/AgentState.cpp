#include "bridge/AgentState.h"

#include "modules/MelodyModules.h"
#include "profile/Offsets_1_21_111.h"

#include <atomic>
#include <cstdio>
#include <string>

namespace eclient_runtime::agentstate {

using namespace eclient_runtime::profile::mc_1_21_111;

// botSelf/botScan/... live in modules::melody; the alias keeps the call
// sites readable without a blanket using-directive pulling the whole
// module namespace into this file.
namespace melody = eclient_runtime::modules::melody;

namespace {

// ── snapshot (writer = game thread, readers = bridge thread) ────────────────
// Seqlock: the writer bumps the sequence to odd, writes, bumps to even. A reader
// that sees an odd sequence, or one that changed across its copy, retries. That
// is what keeps a 20 Hz push from ever holding the game thread: readers never
// take a lock, they retry, and a busy writer just means "no snapshot" for that
// push instead of a stall.
std::atomic<std::uint32_t> g_seq{0};
Snapshot g_shared{};

// ── command ring (producer = bridge thread, consumer = game thread) ─────────
constexpr std::uint32_t kRingSize = 64;        // power of two
Command g_ring[kRingSize];
std::atomic<std::uint32_t> g_tail{0};
std::atomic<std::uint32_t> g_head{0};

// ── live counters ───────────────────────────────────────────────────────────
// Queued counters are bumped on the bridge thread, executed/failed counters on
// the game thread. Atomics, because a plain struct here would be a real race
// between the two.
std::atomic<std::uint64_t> q_attacks{0}, q_rotations{0}, q_inputs{0}, q_dropped{0};
std::atomic<std::uint64_t> e_attacks{0}, f_attacks{0};
std::atomic<std::uint64_t> e_rotations{0}, f_rotations{0};
std::atomic<std::uint64_t> e_inputs{0}, f_inputs{0};

std::atomic_bool g_live{false};
std::atomic<std::uint64_t> g_tick{0};

inline void barrier() { std::atomic_thread_fence(std::memory_order_seq_cst); }

// Index of an actor inside the CURRENT tick's snapshot. publish() ran first in
// this tick, so its actor order is exactly the order botScan produced, which is
// exactly the order botAttackIndex() indexes — no re-lookup, no drift.
int indexOfActorId(std::uint32_t id) {
    const int n = g_shared.actorCount;
    for (int i = 0; i < n; ++i) {
        if (g_shared.actors[i].id == id) return i;
    }
    return -1;
}

bool popCommand(Command& out) {
    const std::uint32_t head = g_head.load(std::memory_order_relaxed);
    if (head == g_tail.load(std::memory_order_acquire)) return false;
    out = g_ring[head];
    g_head.store((head + 1) & (kRingSize - 1), std::memory_order_release);
    return true;
}

Counters liveCounters() {
    Counters c{};
    c.attacksQueued = q_attacks.load(std::memory_order_relaxed);
    c.attacksExecuted = e_attacks.load(std::memory_order_relaxed);
    c.attacksFailed = f_attacks.load(std::memory_order_relaxed);
    c.rotationsQueued = q_rotations.load(std::memory_order_relaxed);
    c.rotationsExecuted = e_rotations.load(std::memory_order_relaxed);
    c.rotationsFailed = f_rotations.load(std::memory_order_relaxed);
    c.inputsQueued = q_inputs.load(std::memory_order_relaxed);
    c.inputsExecuted = e_inputs.load(std::memory_order_relaxed);
    c.inputsFailed = f_inputs.load(std::memory_order_relaxed);
    c.commandsDropped = q_dropped.load(std::memory_order_relaxed);
    return c;
}

} // namespace

// ── capabilities ────────────────────────────────────────────────────────────

Caps caps() {
    // Every field here is a statement about what actually WORKS right now, not
    // about what would be nice. Nothing is reported true speculatively: the app
    // is expected to go dormant on a missing capability, so a lie here would
    // turn into a module that silently does nothing.
    Caps c{};
    c.pose      = true;                                    // botSelf
    c.entities  = true;                                    // botScan
    c.attack    = true;                                    // botAttackIndex
    c.actorType = melody::botMyTypeId(nullptr) != 0;        // own type resolved
    c.rotation  = melody::botRotationAvailable();          // rotation writable
    c.moveInput = (MoveInputElementStride != 0) && (MoveInputVecOffset != 0);
    c.inventory = false;                                   // not wired yet
    return c;
}

void setLive(bool live) { g_live.store(live, std::memory_order_relaxed); }
bool live() { return g_live.load(std::memory_order_relaxed); }

// ── game thread ─────────────────────────────────────────────────────────────

void publish(void* player) {
    if (!player) return;
    if (!g_live.load(std::memory_order_relaxed)) return;   // nobody listening: zero cost

    Snapshot s{};
    s.live = true;
    s.tick = g_tick.fetch_add(1, std::memory_order_relaxed) + 1;
    s.scanMode = 1;
    s.scanRange = kFeedRange;

    float self[10]{};
    if (melody::botSelf(player, self)) {
        s.self.valid = true;
        s.self.x = self[0]; s.self.y = self[1]; s.self.z = self[2];
        s.self.vx = self[3]; s.self.vy = self[4]; s.self.vz = self[5];
        s.self.health = self[6];
        s.self.damaged = self[7] > 0.5f;
        s.self.damageRecent = self[8];
        s.self.ticksSinceDamage = self[9];
    }

    // One scan per tick, the same call the auras use — so every actor the app is
    // shown is an actor this runtime would itself be willing to attack. The app
    // can never be handed a target the module layer would reject.
    melody::BotActor actors[kMaxActors]{};
    const int n = melody::botScan(player, kFeedRange, 1, actors, kMaxActors);
    if (n >= 0) {
        const int count = (n < kMaxActors) ? n : kMaxActors;
        for (int i = 0; i < count; ++i) {
            const melody::BotActor& a = actors[i];
            ActorState& o = s.actors[i];
            o.id = a.entityId;
            o.typeId = a.typeId;
            o.player = melody::botIsPlayerType(a.typeId);
            o.hurt = (a.flags & melody::kBotFlagHurt) != 0;
            o.health = a.health;
            o.x = a.x; o.y = a.y; o.z = a.z;
            o.dist = a.distance;
        }
        s.actorCount = count;
    }

    const std::string funnel = melody::botScanDiag();
    std::snprintf(s.diag, sizeof(s.diag), "%s", funnel.c_str());
    s.caps = caps();
    s.counters = liveCounters();

    g_seq.fetch_add(1, std::memory_order_seq_cst);
    barrier();
    g_shared = s;
    barrier();
    g_seq.fetch_add(1, std::memory_order_seq_cst);
}

void executeQueued(void* player) {
    if (!player) return;

    Command cmd{};
    while (popCommand(cmd)) {
        switch (cmd.type) {
            case CmdType::Attack: {
                const int idx = indexOfActorId(cmd.target);
                if (idx >= 0 && melody::botAttackIndex(player, idx)) {
                    e_attacks.fetch_add(1, std::memory_order_relaxed);
                } else {
                    f_attacks.fetch_add(1, std::memory_order_relaxed);
                }
                break;
            }
            case CmdType::SetRotation: {
                // False until the rotation element layout is derived — the app is
                // told ok:false at queue time, and this counter agrees.
                if (melody::botApplyRotation(player, cmd.a, cmd.b)) {
                    e_rotations.fetch_add(1, std::memory_order_relaxed);
                } else {
                    f_rotations.fetch_add(1, std::memory_order_relaxed);
                }
                break;
            }
            case CmdType::SetInput: {
                if (melody::botApplyInput(player, cmd.forward, cmd.strafe,
                                          cmd.c > 0.5f, cmd.d > 0.5f)) {
                    e_inputs.fetch_add(1, std::memory_order_relaxed);
                } else {
                    f_inputs.fetch_add(1, std::memory_order_relaxed);
                }
                break;
            }
        }
    }
}

// ── bridge thread ───────────────────────────────────────────────────────────

bool snapshot(Snapshot& out) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = g_seq.load(std::memory_order_seq_cst);
        if (before & 1u) continue;                        // writer mid-update
        barrier();
        out = g_shared;
        barrier();
        if (g_seq.load(std::memory_order_seq_cst) == before) return true;
    }
    return false;                                         // busy writer: skip this push
}

std::string diag() {
    Snapshot s{};
    if (!snapshot(s)) return {};
    return std::string(s.diag);
}

bool pushCommand(const Command& cmd) {
    const std::uint32_t tail = g_tail.load(std::memory_order_relaxed);
    const std::uint32_t next = (tail + 1) & (kRingSize - 1);
    if (next == g_head.load(std::memory_order_acquire)) {
        q_dropped.fetch_add(1, std::memory_order_relaxed);   // visible in the push
        return false;
    }
    g_ring[tail] = cmd;
    g_tail.store(next, std::memory_order_release);

    switch (cmd.type) {
        case CmdType::Attack:      q_attacks.fetch_add(1, std::memory_order_relaxed); break;
        case CmdType::SetRotation: q_rotations.fetch_add(1, std::memory_order_relaxed); break;
        case CmdType::SetInput:    q_inputs.fetch_add(1, std::memory_order_relaxed); break;
    }
    return true;
}

} // namespace eclient_runtime::agentstate
