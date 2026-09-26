# Merging the proxy client into the attach runtime — feasibility

**Question asked:** since rebuilding everything (GUI, modules) from scratch is
painful, can we take the existing packet-proxy client, combine it with the
in-process attach runtime, use the proxy/modified pieces to *derive* what the
in-MC runtime needs (reading real state from memory), and eventually stop
depending on the proxy — ending with one "perfect client"?

**Short answer: yes — and the most valuable version of this is not a merged
product but a merged *pipeline*.** The proxy is an outstanding **ground-truth
oracle**; the attach runtime is the right **delivery mechanism**. The mistake
would be to weld them together at runtime, because that keeps every proxy
downside (extra app, relay latency, two connections, codec pinning) while the
in-process build needs none of them.

Everything below is grounded in what the two projects actually contain today.

---

## 1. What each side is actually good at

| | proxy client (`Eclient`, Java) | attach runtime (this repo) |
|---|---|---|
| Sees | **semantic truth**: decoded packets — `EntityTracker` (netId → entity), positions, rotation, health, `ItemData`, inventory, `PlayerAuthInputPacket` (motion + input flags), `SetEntityMotionPacket` | raw memory with validated offsets; zero latency; no extra process |
| Acts by | `session.serverBound(pkt)` / `clientBound(pkt)`, plus `cancel()` / `cancelAndReplace()` on the packet bus | calling/writing the game's own structures (`Attack()`, ECS elements, patches) |
| Cost | RakNet relay + Cloudburst codec pinned to `3.0.0.Beta6-SNAPSHOT`; auth chain; a second connection; version locks | every single field must be *derived* from the binary (the whole reason this repo exists) |
| Modules | 76 packet-driven modules + chat command system + Friends/Enemies enforced at the attack-packet choke point | 8 PvP-focused modules, memory-derived |
| **GUI** | **Jetpack Compose in a system overlay** (`ui/overlay/OverlayService.kt` + `ESPOverlayView`, foreground Service, `WindowManager`) — it already draws over *any* app, Minecraft included | native in-game panel (`InGameGui`) |

The asymmetry that matters: **the proxy already knows the meaning of the data
the attach build has to reverse-engineer.** That is the entire feasibility
argument.

## 2. Model A — proxy as derivation oracle (high value, low risk, start here)

Run the proxy *during development only*, with the attach build on the same device,
and correlate its decoded truth against memory snapshots:

```
proxy:   me: pos=(123.5, 64.0, -88.25) yaw=137.5 pitch=-4.2 hp=20
         entities: [netId 42 → minecraft:player, 55 → minecraft:zombie]
                    ↓ (same moment, on device)
attach:  ECSDump: pool 0x75df36b7 ActorRotationComponent, element deltas: …
```

This turns the current bottleneck — "derive the element layout of
`MoveInputComponent` / `ActorRotationComponent` from a device dump and eyeball
which number changed" — into "match a *known labelled value* to a memory slot".

Concretely it collapses these from guesswork into lookup:

| Unknown today | Oracle answers it |
|---|---|
| `MoveInputComponent` layout (blocks walking) | input vector the game sends in `PlayerAuthInputPacket` |
| `ActorRotationComponent` stride + yaw/pitch offsets (blocks aim + steering) | yaw/pitch in `MovePlayerPacket` / auth input |
| player type id semantics | the entity's type/name from `EntityTracker` |
| `MobHurtTimeComponent` | hurt events + i-frames visible on the wire |
| inventory (AutoTotem) | `ItemData` / inventory content packets |

Implementation sketch (no new RE, ~a day of glue):
1. Proxy side: a `TruthProbe` module (the client already has `PacketCollector`
   and `FlightProbe` for exactly this pattern) that emits JSONL once per tick:
   `{t, self:{pos,yaw,pitch,hp,onGround}, entities:[{netId,type,name,pos,hp}]}`.
2. Host side: the attach build's `ECSDump` already snapshots pool elements with
   deltas; add a "labelled" mode that dumps the *same tick* it receives a truth
   line (loopback socket on `127.0.0.1`, which `LoopbackBridge` exists for and is
   currently dead code — this is its obvious use).
3. Match: known inputs (walk forward, stand, rotate) → identical deltas → the
   offset is *proven*, and gets written into `Offsets_1_21_111.h` with the oracle
   run as its evidence.

## 3. Model B — shared capability layer (medium term)

Give both sides one module interface, with two backends:

```kotlin
interface Backend {
  fun selfPose(): Pose
  fun entities(): List<Entity>
  fun attack(id: EntityId)
  fun setRotation(yaw: Float, pitch: Float)
  fun setInput(forward: Float, strafe: Float, jump: Boolean, sneak: Boolean)
  fun inventory(): List<Item>
}
// PacketBackend = proxy (exists today, in another repo)
// MemoryBackend = this repo (partially: pose/entities/attack)
```

Then modules are written once. Realistic transfer rate of the proxy's 76
modules: **~60–70 % are expressible with memory + game calls** (auras, hitbox
visuals, sprint/input tricks, ESP-style readers — anything that reads state or
writes the local client's own state). The rest genuinely need to *send or
rewrite packets*:

* transaction piggybacking on `PlayerAuthInputData` flag bits,
* server-side inventory moves (AutoTotem-class, container actions),
* relay-style tricks that depend on editing traffic in flight.

Those need the in-process equivalent: locate the game's own packet-send path and
call it (or hook it), which is a *separate* RE workstream. The proxy's
`docs/PROTOCOL.md` is the map for it: it already lists which classes and fields
each trick depends on (`PlayerAuthInputPacket` #2 by blast radius, 34 files).

## 4. Model C — drop the proxy entirely (the end state)

Prerequisites, in order:

1. **Memory parity for reads** (Model A delivers this): pose, rotation, health,
   entities — today the attach build reads pose/health/entities, but *not*
   inventory, containers or entity metadata.
2. **In-process send path** for the packet-level features listed above.
3. **Identity**: a stable unique id per entity (the proxy keys on the protocol's
   long unique id; the attach build currently has the packed ECS entity id,
   which is stable within a session and is enough for combat, but the protocol
   id is the right long-term key).

What this buys: one APK, no relay, no relay latency, no codec pinning treadmill,
no second connection, and no "the proxy is between me and the server" failure
mode. What it costs: real RE time on (2) and (3).

**Not transferable at all:** anything server-authoritative. Enlarged hitboxes
were done by *sending* `SetEntityData`; in-process you either call the game's own
send path (server sees it, same as the proxy) or you fake it locally (visual
only, server rejects the reach). The proxy never had a magic ability here — it
just had a convenient place to lie on the wire.

## 5. Concrete merge shape (file-level, verified against both trees)

The seam is already a Kotlin interface, not a packet:

```kotlin
// proxy, app/src/main/kotlin/com/nexoraclient/module/BaseModule.kt
abstract class BaseModule(
    val name: String, val category: ModuleCategory, val description: String = ""
) : PacketEventBus.PacketListener {
    protected fun launchTickLoop(intervalMs: Long, block: suspend () -> Unit): Job
    // settings: Bool / Float / Int / Enum / String, all registered in `settings`
}
```

and the registry is a singleton Compose can observe:

```kotlin
object ModuleManager {                 // registerAll(...), version: StateFlow<Int>
```

Three facts follow, and they decide the whole design:

1. **Most modules don't need packets — they need *state* and *actions*.**
   `launchTickLoop` is a polling loop; it does not care whether the numbers come
   from a decoded `MovePlayerPacket` or from a memory read. A `MemoryBackend`
   exposing `selfPose() / entities() / inventory() / attack() / setRotation() /
   setInput()` satisfies the same call sites the packet path does.
2. **The eating guard already exists on the proxy side** —
   `BaseModule.launchTickLoop` has `pauseWhileEating` + `com.rubidiumclient.core.EatingGuard`.
   The "AlwaysSprint must not break gapple eating" requirement is one line of
   reuse (`pauseWhileEating = true`), not new logic in the native layer.
3. **Namespaces are already kin.** The proxy's classes declare
   `com.rubidiumclient.*`; this repo's loader is
   `com.rubidiumclient.host.EClientLoaderProvider`. These are two halves of one
   product family, not two strangers.

Target picture (two processes, one product):

```
┌─ controller app (today's proxy) ────────┐   ┌─ patched Minecraft (this repo) ─┐
│ ModuleManager + BaseModule + settings   │   │ native runtime + bot brain      │
│ OverlayService (Compose clickgui/HUD)   │◄──│ LoopbackBridge 127.0.0.1:38170  │
│ ChatCommands, config, EatingGuard       │──►│ GameBridge::snapshot() + verbs  │
│ [relay/, CodecRegistry, auth, sessions] │   │ Kotlin dex append (classesN.dex)│
└────── delete this half ─────────────────┘   └─────────────────────────────────┘
```

What moves where:

| Piece | Fate |
|---|---|
| `ui/**` (Compose overlay, dashboard, theme) | **kept as-is** — transport-independent |
| `module/BaseModule`, `ModuleManager`, settings, categories | **kept as-is** — the seam |
| `events/PacketEventBus` | kept as the *in-app* bus; fed by the memory backend instead of the relay |
| `core/EatingGuard`, `config/`, chat commands | kept |
| `core/relay/**`, `relay/Protocol`, auth chain, `RealmsApi`, LAN scanner | **removed** once Model C completes |
| modules that only mutate traffic in flight | rewritten against `MemoryBackend` verbs (or dropped) |

Two engineering items that must not be forgotten:

* **Localhost is not a trust boundary.** `LoopbackBridge` serving state on
  `127.0.0.1:38170` is reachable by *any* app on the device (both APKs hold
  `INTERNET`). The bridge needs a per-install shared token (and ideally
  `SO_PEERCRED`-style verification) before it serves anything sensitive.
* **Keep the brain in-process.** The bot's per-tick decisions must stay in the
  native/Kotlin layer inside MC; the controller sends *intents and settings*, not
  per-tick inputs. Then a dead controller costs you the HUD, never the fight.

## 6. The GUI question — why "from scratch" stops being necessary

The ClickGUI pain dissolves once you look at where the proxy's GUI actually
lives. It is **not** an in-game render hack:

* `ui/overlay/OverlayService.kt` — an Android foreground `Service` that attaches
  a `ComposeView` through `WindowManager` (`PixelFormat`, notification channel,
  `androidx.compose.material3.*`), i.e. a **system overlay that draws on top of
  any app** — including real Minecraft.
* `ui/overlay/ESPOverlayView.kt`, `OverlayState.kt`, `OverlayNotifier.kt` — the
  HUD/ESP surface and its state.
* `ui/dashboard/DashboardActivity.kt` + `ui/theme/RibidiumTheme.kt` — the
  config surface.

Nothing in that stack knows a packet exists. So the "perfect client" does not
need a from-scratch in-game GUI at all — **the GUI is the controller app's
overlay, and it moves data sources, not code.**

If you later want *one* APK with no second app, the honest blocker is **not
code**: this repo already appends Kotlin dex into the patched APK
(`classes6.dex`, 2.2 MB, verified in CI), so moving `ModuleManager` + `BaseModule`
inside is the mechanism that already exists. The blocker is **Android
resources** — a prebuilt third-party APK's resource table cannot be extended by
dex append, and Compose/Material needs resources. Realistic options, cheapest
first:

1. **Controller app keeps the GUI** (recommended) — overlay draws over MC.
2. **GUI drawn natively in-process** — this repo already draws an in-game panel
   (`InGameGui`); a richer GUI is native work, but zero resource merging.
3. **Full Compose inside the patched APK** — needs genuine APK resource surgery
   (aapt2 merge against the vanilla resource table). Possible, expensive, do it
   last, only if option 1 is unacceptable.

## 7. Recommendation

1. **Now (this week):** Model A. Build the truth probe in the proxy, the labelled
   dump in the attach build, and use the pair to finish the four layouts that
   currently block everything (`MoveInputComponent`, `ActorRotationComponent`,
   `MobHurtTimeComponent`, inventory). This is the cheapest possible path to a
   fully working bot, and it makes every future offset derivation minutes instead
   of evenings.
2. **Next:** Model B's interface, but only for modules that both sides can
   express today (readers + local-state writers). Do not attempt packet-send
   parity yet.
3. **Then:** Model C for shipping, with (2)/(3) from §4 as its own project.

**Verdict: feasible, recommended, and the ordering above matters more than the
merging itself.** The proxy's real gift to this repo is not code sharing — it is
*labelled data*, which is exactly what a memory-first client is starving for.
And the ClickGUI does not have to be rebuilt anywhere (§6): it is already an
overlay that draws over Minecraft and only ever needed a different data source.
