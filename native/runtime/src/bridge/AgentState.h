#pragma once

// AgentState — the data plane between the game thread and the loopback agent.
//
// P0 of docs/MERGE_PLAN.md. The bridge used to answer with a status blob built
// from GameBridge; it now speaks the module-SDK contract (docs/MODULE_SDK.md),
// which needs two things GameBridge does not have:
//
//   1. a per-tick snapshot of the WORLD (self + nearby actors), because the only
//      place allowed to touch game memory is the game thread, and
//   2. a way for the app to ask for actions without ever running on the game
//      thread itself (attacks must execute inside the tick).
//
// So: publish() latches a snapshot on the game thread; the bridge thread only
// copies it (seqlock) and serializes it. Intents come back as queued commands
// and are executed by executeQueued() on the next tick.
//
// Fail-closed, like everything else in this runtime: no player pointer, no live
// client, or an underived layout simply means "no data / not executed", never a
// crash and never a lie — every unavailable capability is reported as missing.

#include <cstdint>
#include <string>

namespace eclient_runtime::agentstate {

// botScan clamps its range to 32 m, so the feed can never see further than that.
inline constexpr float kFeedRange = 32.0f;

// Actors latched per tick. The index of an actor here is the SAME index
// botAttackIndex() expects, because this list is exactly what the tick's
// botScan returned — one scan serves both the push and the attacks.
inline constexpr int kMaxActors = 48;

struct SelfState {
    bool  valid{false};
    float x{0}, y{0}, z{0};
    float vx{0}, vy{0}, vz{0};
    float health{0};
    bool  damaged{false};
    float damageRecent{0};      // damage events in the last 5 s
    float ticksSinceDamage{-1}; // -1 = never
};

struct ActorState {
    std::uint32_t id{0};        // packed entity id — stable across ticks
    std::uint32_t typeId{0};
    bool  player{false};
    bool  hurt{false};
    int   health{0};
    float x{0}, y{0}, z{0};
    float dist{0};
};

struct Caps {
    bool pose{false};       // self position/velocity/health readable
    bool entities{false};   // actor list readable
    bool actorType{false};  // own type id resolved (drives the player flag)
    bool attack{false};
    bool rotation{false};   // ActorRotationComponent layout derived
    bool moveInput{false};  // MoveInputComponent layout derived
    bool inventory{false};  // not wired yet
};

struct Counters {
    std::uint64_t attacksQueued{0}, attacksExecuted{0}, attacksFailed{0};
    std::uint64_t rotationsQueued{0}, rotationsExecuted{0}, rotationsFailed{0};
    std::uint64_t inputsQueued{0}, inputsExecuted{0}, inputsFailed{0};
    std::uint64_t commandsDropped{0};   // ring was full
};

struct Snapshot {
    bool  valid{false};
    bool  live{false};          // a client is connected; publish() is gated on this
    std::uint64_t tick{0};
    SelfState self{};
    ActorState actors[kMaxActors]{};
    int   actorCount{0};
    int   scanMode{1};          // 1 = players, fallback to anything (see MelodyModules)
    float scanRange{kFeedRange};
    Caps  caps{};
    Counters counters{};
    char  diag[512]{};          // last funnel line (botScanDiag)
};

enum class CmdType : int { Attack = 1, SetRotation = 2, SetInput = 3 };

struct Command {
    CmdType type{CmdType::Attack};
    std::uint32_t target{0};    // Attack: packed entity id
    float a{0}, b{0};           // SetRotation: yaw, pitch
    float c{0}, d{0};           // SetInput: jump, sneak
    float forward{0}, strafe{0};
};

// ── game thread ──────────────────────────────────────────────────────────────
// Latch this tick's world state. Cheap no-op unless a client is connected.
void publish(void* player);
// Execute intents queued by the bridge thread. Runs right after publish(), so
// the actor indices it uses are the ones from this tick's scan.
void executeQueued(void* player);

// ── bridge thread ────────────────────────────────────────────────────────────
bool snapshot(Snapshot& out);      // consistent copy (seqlock; never blocks the game)
Caps caps();                       // same capability verdict the push carries
std::string diag();                // last funnel line, "" when none yet
void setLive(bool live);           // connection gate for publish()
bool live();

// Queue one intent. False = ring full (the counter records it; nothing is lost
// silently).
bool pushCommand(const Command& cmd);

} // namespace eclient_runtime::agentstate
