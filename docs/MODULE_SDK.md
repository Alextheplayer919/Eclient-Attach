# Module SDK — the environment for writing modules

Part of the confirmed design (`docs/PROXY_MERGE_FEASIBILITY.md` §8.7): **Eclient
and Minecraft are both modified and connected.** Eclient holds the GUI and the
module brain; a thin agent inside MC reads memory, decodes it, and executes
intents. No proxy in the path.

This document is the contract between the two halves, so module development can
start **now**, against a surface that will not move when offsets do.

**Drop-in source for Eclient lives outside this repo's build** (`eclient-sdk/` in
the workspace, package `com.rubidiumclient.agent`) — nothing here changes this
repo's boot chain or CI.

## The rule that makes it stable

Modules never see offsets, addresses, component hashes or cores:

```kotlin
Backends.active.selfPose()
Backends.active.entities()
Backends.active.inventory()
Backends.active.attack(id)
Backends.active.setRotation(yaw, pitch)
Backends.active.setInput(forward, strafe, jump, sneak)
```

When Minecraft updates, the **agent** changes — one place you maintain
occasionally. Module code never does. That is the entire architecture in one
sentence.

## Backends

| Backend | When | Notes |
|---|---|---|
| `AgentBackend` | patched MC running with the agent | normal target |
| `PacketBackend` | plain MC + proxy relay | today's shipping path, untouched |
| `ReplayBackend` | **offline**, from a recorded session file | develop modules with MC closed |
| `NullBackend` | nothing attached | inert, never throws |

Modules declare needs and degrade:

```kotlin
val requires = setOf(Cap.Pose, Cap.Entities, Cap.Rotation)
if (!Backends.active.caps.containsAll(requires)) return   // dormant, not broken
```

The agent publishes its own caps in every state push, so a module always knows
what the current build can actually do. Today: `ActorType`, `Attack`, `Pose`,
`Entities`. `MoveInput` and `Rotation` join the list when those layouts are
derived — **no module source changes when they do.**

## Wire contract (agent ↔ app)

Newline-delimited JSON, TCP `127.0.0.1:38170` (the port `LoopbackBridge` already
listens on), token on every line.

**app → agent**
```json
{"token":"…","id":18,"op":"attack","target":42}
{"token":"…","id":19,"op":"setRotation","yaw":137.5,"pitch":-4.2}
{"token":"…","id":20,"op":"setInput","forward":1.0,"strafe":0.0,"jump":false,"sneak":false}
{"token":"…","id":21,"op":"toggleModule","module":"KillAura2","enabled":true}
{"token":"…","id":22,"op":"setSetting","module":"KillAura2","setting":"Range","value":4.5}
```

**agent → app** (state push 10–20 Hz, plus acks)
```json
{"push":"state","t":1234567,
 "self":{"x":123.5,"y":64.0,"z":-88.25,"yaw":137.5,"pitch":-4.2,"hp":20,"onGround":true},
 "entities":[{"id":42,"type":319,"player":true,"x":…,"y":…,"z":…,"dist":3.2,"hp":20,"hurt":false}],
 "inventory":[{"slot":0,"id":"minecraft:totem_of_undying","count":1}],
 "caps":["Pose","Entities","ActorType","Attack"],
 "missing":["MoveInput","Rotation"]}
{"id":18,"ok":true}
```

**Implementation status (r36, `patched-r36`):**

| op | status |
|---|---|
| `getState` | live — returns a state push on demand |
| `ping` | live |
| `diag` | live — the bot funnel line (`botScanDiag`), so "sees nothing" always has a reason |
| `attack` | live — queued, executed on the next tick through the game's own attack call |
| `setRotation` | **refused** (`ok:false`, "rotation layout not derived") until `ActorRotationComponent` is derived |
| `setInput` | **refused** (`ok:false`, "move input layout not derived") until `MoveInputComponent` is derived |
| `toggleModule`, `setSetting`, `listModules` | **live** — applied on the game thread against the native ModuleManager, mirrored into the Kotlin registry, so the app IS the GUI |
| `readMem`, `writeMem` | **do not exist** — unknown op, by design |

State push carries `self`, `entities` (nearest-first, ≤32 m, the same filter the
auras use), `modules` (name/category/enabled/kotlin), `caps`, `missing`, `cmds`
(queued/executed/failed/dropped counters) and `diag`, at ~20 Hz.

Rules, non-negotiable:

1. Token required on **every** line; a wrong token drops the connection. Localhost
   is not a trust boundary — any app on the device can reach this port.
2. **No `readMem` / `writeMem` op, ever.** The app asks for meaning, never
   addresses. That is what keeps a localhost socket from becoming an
   arbitrary-write primitive into Minecraft.
3. Unresolved capability ⇒ `ok:false` + reason. The app never crashes; the module
   goes dormant.
4. **Sub-frame decisions stay in-process.** State out and intents in at 10–20 Hz is
   fine; "rotate and swing now" is not — 50 ms round-trips miss. The bot brain
   runs inside MC; the app steers it.

## What this buys, concretely

* Write a module today, run it live later, with **zero changes** when `MoveInput`
  and `Rotation` go live.
* Develop with **no Minecraft at all**: `ReplayBackend("session.jsonl")` replays
  recorded state at any speed and collects every intent a module fires, so module
  decisions are testable and deterministic.
* The volatile surface shrinks to one file the agent owns — offsets — instead of
  leaking into every module.

## Order of work

1. Drop `eclient-sdk/` into Eclient, register `AgentClient`. With nothing running,
   `Backends.active` reports `dormant` — nothing breaks.
2. Implement the wire contract on the agent side (`LoopbackBridge` + verbs). State
   flows; read-only modules work immediately.
3. Derive `MoveInput` + `Rotation` via the oracle step. Walking and aiming go live;
   `caps` grows two entries and modules light up on their own.
4. Module development is then free: replay for logic, live for behaviour.
