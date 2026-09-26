# MERGE_PLAN.md — building the memory-backed client, without breaking anything

The destination: **Eclient (GUI + module brain) and the patched Minecraft (agent)
connected directly, no proxy.** This is the build path to get there in steps that
can each be verified and left alone if they turn out wrong.

Not a rewrite. Every phase is **additive**, on both sides, and the currently
shipping proxy build keeps working the whole time.

---

## 0. What the two repos actually are (measured, not assumed)

**Eclient** — 81 module files, Kotlin. Directory `com/nexoraclient/`, package
`com.rubidiumclient.*` (renamed in progress; the directory only matters for
placement, not compilation).

| Fact | Number |
|---|---|
| modules reading the state singleton `EntityTracker` | **69 / 81** |
| modules touching `PacketEvent` | 72 |
| modules calling `.cancel()` on packets | 53 |
| modules using `RubidiumRelaySession` | 37 |
| modules that neither send nor cancel anything | **16** |
| modules that send nothing at all | 16 |

The single most important measurement: **69 modules consume `EntityTracker`**, a
singleton — not the network. `EntityTracker` is fed today by
`core/relay/listener/GamingPacketListener.kt`, and it exposes:

```kotlin
object EntityTracker : PacketEventBus.PacketListener {
    data class TrackedEntity(runtimeId, uniqueId, identifier, type,
                             x, y, z, yaw, pitch, headYaw, velX, velY, velZ,
                             health, maxHealth, armorValue, isOnGround, isSneaking,
                             isSprinting, hurtTime, …)
    private val entities = ConcurrentHashMap<Long, TrackedEntity>()
    @Volatile var selfRuntimeId / selfUniqueId / selfX / selfY / selfZ / selfYaw / selfPitch
    private val selfInventory / selfArmor = ConcurrentHashMap<Int, ItemData>()
}
```

**That object is the seam.** Feed it from memory and 69 modules receive live state
**without a single edit to module code.**

Top packets modules construct, i.e. the mapping table to size (uses across the tree):

```
PlayerAuthInputPacket 90   MovePlayerPacket 35   SetEntityDataPacket 11
SetEntityMotionPacket 41   UpdateAbilitiesPacket 28   InventoryTransactionPacket 9
TextPacket 35              PlayerActionPacket 13   AnimatePacket 6
```

**Eclient-Attach** — the agent side, everything already in place:

| Piece | Where | State |
|---|---|---|
| tick entry | `MelodyModules.cpp::onNormalTick(void* localPlayer)` @ 831 | live |
| world data for a state push | `botScan(player, maxRange, mode, out, cap)` + `botScanDiag()` | live |
| attack | existing aura/attack path | live |
| status socket | `LoopbackBridge` — started from `native/runtime/src/main.cpp`, binds `127.0.0.1:38170` | serves a 250 ms status blob: no token, no verbs |
| boot chain | `EClientLoaderProvider` → `loadLibrary` → `JNI_OnLoad` → patch manager → `gameplay::initialize()` | **inviolable** |
| posture | `DO_NOT_BREAK.md` — fail-closed, inert on unresolved prerequisite | enforced |

So the gap is small and specific: **the bridge needs to be an agent, and the app
needs to feed its tracker from it.**

---

## 1. Non-negotiables for every phase

1. **Boot chain untouched.** No new mandatory init on the attach side. The bridge
   stays optional and non-fatal: if it fails, the game plays and the runtime is inert.
2. **No raw memory over the wire.** Semantic verbs only. `readMem`/`writeMem` will
   never exist — otherwise any app on the device gets arbitrary write into Minecraft
   via `127.0.0.1`.
3. **Token on every line.** Generated on first boot, stored in the app's private
   dir, shown in the in-game panel for one-time pairing.
4. **One writer per side.** State snapshot is latched on the game thread in
   `onNormalTick`; only the push thread serializes it. The push thread never reads
   game memory (that is both a data race and the `/proc/self/maps` per-read trap).
5. **Additive in Eclient** until a mode switch explicitly selects memory mode. The
   relay path is not deleted in any early phase.
6. **One phase = one commit → CI → read logs → green** before the next.
7. **Both engines never run at once** (double actions). The mode switch enforces it.

---

## 2. Phases

### P0 — `LoopbackBridge` becomes an agent (attach repo, ~1 commit)

Keep the port and the file; replace the payload. New: token, NDJSON, verbs, push at
~20 Hz, and a capability list.

```jsonc
// agent -> app, every ~50 ms
{"push":"state","t":…,
 "self":{"x":…,"y":…,"z":…,"yaw":…,"pitch":…,"hp":…,"onGround":…},
 "entities":[{"id":…,"type":…,"player":…,"x":…,"y":…,"z":…,"dist":…,"hp":…,"hurt":…}],
 "caps":["Pose","Entities","ActorType","Attack"],
 "missing":["Rotation","MoveInput","Inventory"]}

// app -> agent
{"token":"…","id":1,"op":"getState"}
{"token":"…","id":2,"op":"attack","target":42}
{"token":"…","id":3,"op":"setRotation","yaw":137.5,"pitch":-4.2}   // ack ok:false until derived
{"token":"…","id":4,"op":"setInput","forward":1,"strafe":0,"jump":false,"sneak":false}
{"token":"…","id":5,"op":"toggleModule","module":"KillAura2","enabled":true}
```

`caps` is the honesty mechanism: an op whose layout is not derived yet acks
`ok:false` with a reason, and the app goes dormant instead of pretending.

**Gate:** a test client gets valid state lines and correct `ok:false` for
unavailable ops; CI green; the game still boots with the bridge disabled.

### P1 — Eclient reads state from memory (Eclient repo, additive)

Two new files in `com.rubidiumclient.agent` (already written: see the SDK folder),
plus **one additive function** in `EntityTracker`:

```kotlin
// EntityTracker gains exactly one ingest entry point; nothing else changes
internal fun ingest(self: RemoteSelf, actors: List<RemoteActor>) { … }
```

`AgentTrackerFeed` maps the agent push onto that call. Modules keep reading
`EntityTracker.selfX`, `EntityTracker.entities`, exactly as before.

**Gate — and this is where the oracle becomes a test:** run the same world twice,
once through the proxy (`GamingPacketListener`) and once through memory, and
diff the resulting tracker state. Report per-field tolerances (positions should
match to float precision; health to the same tick). That diff is both the proof
that memory state is correct and the tool that finds any wrong offset.

At the end of P1: **69 modules see the world in memory mode. Zero module edits.**

### P2 — action routing (Eclient repo, additive)

`BackendMode { PROXY, MEMORY, AUTO }`, default `AUTO` (memory when the agent is
connected and healthy, otherwise proxy). A `MemorySession` implements the session
surface modules use and maps the packets that have memory equivalents:

| module sends | memory equivalent |
|---|---|
| `PlayerAuthInputPacket` (input + motion) | `setInput` / `setRotation` / sprint flag |
| `MovePlayerPacket` (rotation) | `setRotation` |
| `PlayerActionPacket`, `AnimatePacket` | `attack(id)` |
| `TextPacket` (chat) | host chat send |
| `SetEntityDataPacket`, `SetEntityMotionPacket` (fake visuals) | local-only or unsupported |
| `InventoryTransactionPacket`, `ItemStackRequestPacket` | later (needs inventory caps) |
| `UpdateAbilitiesPacket`, `RequestAbilityPacket` | later (ability writes) |

Unmapped packets are **dropped and counted**, surfaced in the panel. Nothing
silently "works" when it does not.

**Gate:** toggle a module in MEMORY mode → the verb shows up in the agent log; the
unmapped-packet counter is visible and non-zero where expected.

**"Not a proxy" acceptance test (P2 exit criterion):** in MEMORY mode the app must
start **no relay listener, no upstream connection, no codec, no auth chain** — the
game connects to its server directly. Verify by: no listening RakNet socket, no LAN
ping response, and the panel reporting `memory` as the active engine.

### P3 — capability completion (attach repo)

Derive `MoveInputComponent` and `ActorRotationComponent` (stride + yaw offset) with
the oracle, then flip them on. `caps` gains `MoveInput` and `Rotation`; **no module
code changes** — `ExampleAimAssist` and the walking bot light up on their own.

**Gate:** the same module runs unchanged in replay, then live, and the bot walks and
aims with the write guards (`hash && stride && offset`) satisfied.

### P4 — end-state cleanup (Eclient repo, last, optional)

Only after MEMORY mode has proven itself across several sessions: move
`core/relay/**`, `relay/Protocol`, auth, LAN/Realms behind a build flavor or delete
them. Keep `PacketBackend` for unpatched devices — it costs nothing to keep and it
is the only build that works without patching.

---

## 3. Risk register (with the mitigation already chosen)

| Risk | Mitigation |
|---|---|
| Minecraft update breaks offsets | offsets are data (`Offsets_1_21_111.h`), runtime goes inert, never crashes; the oracle re-derives in minutes |
| Packet-only modules do not transfer | measured: 16 modules are pure readers; the rest are mapped or explicitly dropped, per P2 table |
| Double actions (both engines live) | mode switch; never both |
| Bridge reached by another app | per-install token; semantic verbs only; no raw memory op |
| Frame hitches | snapshot latched on game thread, serialized off-thread; no per-read `/proc/self/maps`; no logging in the tick path |
| Silent failure | `caps`/`missing` + unmapped-packet counter + `ok:false` reasons — the panel always says what is unavailable and why |
| Breaking the attach build | no boot-chain edits, additive files only, CI per phase, `DO_NOT_BREAK.md` invariants unchanged |

## 4. What "done" looks like

One APK (patched MC, running the agent) + one app (Eclient: GUI, modules, config).
The app opens no network listener and connects to nothing; the game talks to its
server directly; the app receives memory-derived state and sends semantic intents.
Modules are written against `Backends.active` and never see an offset, so the
client survives game updates by updating one file.

**Start with P0.** It is self-contained, it cannot break the game, and it is the
only thing standing between this plan and P1's tracker parity test.
