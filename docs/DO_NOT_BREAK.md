# DO-NOT-BREAK contract — E-Client 1.21.111 attach runtime

This runtime works. Changes are allowed only with full understanding of the
delicate chain below. Baseline (known-good, straight from the shipped zip):
commit `082d9cf`.

## The boot chain (order matters)

1. APK install → Android instantiates **ContentProviders before
   `Application.onCreate`** → `com.rubidiumclient.host.EClientLoaderProvider.<clinit>`
   runs `System.loadLibrary("eclient_host")`.
2. `JNI_OnLoad` (`src/host/JniBootstrap.cpp`) stores the `JavaVM*` for the GUI
   and starts `MinecraftHost::bootstrapLoop`.
2b. The provider's `onCreate` (smali) registers activity-lifecycle callbacks;
    `onActivityResumed` then calls, in order: `nativeTouchReady()`,
    `ModuleRegistry.init()`, `EClientUi.attach(activity)`.
    **r25: `EClientTouchListener` is NOT INSTALLED any more.** It used to sit on
    the decor view, i.e. inside the game's own input dispatch path; the Views
    overlay now owns its touches through its own `WindowManager` sub-windows, so
    nothing of ours belongs in that path. The listener smali file and the
    `nativeGuiTouch(IIFF)Z` export are still shipped so the touch ABI and the
    patcher's listener greps stay intact — they are simply never wired in.
    **Do NOT remove the `nativeTouchReady()` call**: it is the KotlinBridge bind
    point (`KotlinBridge::bind`) — without it no Kotlin module ever ticks.
    Names/descriptors must stay in lockstep with the smali; they are ABI.
3. Bootstrap loop polls `dl_iterate_phdr` (250 ms) until **unmodified**
   `libminecraftpe.so` appears → `GameBridge` sanity profile →
   `PatchManager` fail-closed validation of all 18 byte sites →
   `GameplayModules` Dobby hooks (NormalTick, SpeedHack) →
   `InGameGui` (Dobby: eglSwapBuffers ± eglCreateWindowSurface) →
   input route (view-layer listener; **no native key hook — hardware buttons
   are not used; the floating toggle bar is the only menu switch**).

## GUI invariants (learned the hard way)

**r26 — the live UI is Android Views. ImGui/GL is dormant: `InGameGui::initialize()`
returns without installing the swap hook, so none of the ImGui code below runs.**

- **Never reintroduce a silent mount.** The chip is mounted before the module
  registry, prefs and every JNI call, so a broken subsystem cannot take the GUI
  with it. If a mount rung fails it must be *recorded* (`attempts`, the log file,
  the chip label showing the winning strategy) — "nothing appears and nobody
  knows why" is the exact failure r26 was written to make impossible.
- Mount order is PANEL → INJECTED → OVERLAY on purpose. `INJECTED` (a child of the
  game's content frame) is the rung that cannot fail for token/permission/z-order
  reasons; do not delete it, and do not move it after OVERLAY.
- Three independent proof-of-life signals must stay: the native toast (fired once
  from `nativeTouchReady`), the Kotlin toast, and the chip label with the strategy
  + heartbeat. They localise a fault to native / dex / window layer in one glance.
- The log file (`Download/EClient/eclient_ui_*.txt`) is part of the product, not
  debug leftovers: it is how a device-only failure gets diagnosed without adb.

## PvP Bot invariants (r27)

- **No target → no writes.** With nothing in range the bot must not touch
  movement or attack. Every "automation gone wrong" failure the user could hit
  starts with a bot that kept acting when it had nothing to act on.
- **The bot shares KillAura2's actor filter** (`melody::killauraTargetValid`).
  Do not give `botScan` its own validity rules: the moment the two disagree, the
  bot can act on an actor the module refuses and the difference is invisible.
- **Hit attribution keys on the entity id**, never on the scan index (indexes
  reshuffle every tick). The id travels through a float slot bit-exact
  (`memcpy` native side, `floatToRawIntBits` Kotlin side — NOT `toBits()`, which
  canonicalises NaN and would corrupt an id).
- **The bot's health history is its own map** (`g_botHealth`), deliberately not
  `g_hurtState`: that one feeds KillAura2's Hurttime filter and must not learn
  about the bot's bookkeeping.
- **Movement writes go through `botMove` (KotlinBridge)**, not a Melody-side
  helper: only there is `g_prevOut` updated, which is what keeps
  `getWorldInputDir` reporting the *human's* input instead of the bot's own
  velocity (that is how Human-override works at all).
- **Rotation is a pointer field, not a pool element.** `*(float**)(actor + 0x218)`
  is a `Vec2{yaw, pitch}` (yaw first). `botRotationAvailable()` reports the
  *probed* truth through an atomic written on the game thread; never compute it
  on the bridge thread, which must not touch game memory at all. The old
  `ActorRotationComponentHash`/stride hunt is retired — do not reintroduce it.
- **Fly goes through abilities, never through velocity or position.** Granting
  the engine's own `Flying`/`MayFly` abilities makes the game fly the player;
  the two earlier mechanisms (writing the state vector's velocity, then writing
  position/prevPos) both fought the engine and were deleted. If you add a
  movement module, do not resurrect either of them.
- **An ability that was granted must be given back.** `tickFlyDisable()` clears
  Flying/MayFly/Ladder when the module is switched off, and it runs *before*
  the world-ready gate for exactly that reason. A module that leaves the player
  flying after being disabled is a bug, not a feature.
- **A memory read must never answer "unreadable" for memory that IS readable.**
  The map cache re-checks on a miss (per-address dedup + a 16-per-250 ms budget
  + a 100 ms negative cache bound the cost). The first version rate-limited
  refreshes outright, which meant every read into a freshly mapped region
  failed for up to 250 ms — the game maps things constantly, so scans and
  position reads silently returned nothing. `run.sh bench` asserts a freshly
  mapped region is readable on the FIRST read; keep it green.
- **A benchmark must print what it measured.** The first version of the tick
  bench reported 0.2 us/tick while the modules were doing nothing at all. Any
  perf harness here prints its work counters (actor lists, attacks, ability
  calls) next to the timing, so "fast" cannot be confused with "not running".
- **An un-derived offset must fail inert.** Every API pointer goes through
  `apiPtr<T>(offset)`, which returns null for offset 0. Calling `fn(0)` would
  jump into the module base.
- **Movement is input-based walking, never a velocity write.** `botWalk` writes
  the joystick's own state (`MoveInputComponent`) and lets the engine move the
  player. Do not reintroduce the fly-style velocity write for the bot: it fights
  the engine every tick, animates nothing and cannot respect water/ice/ladders.
  If walking is unavailable the bot stands still and reports why — a bot that
  moves badly is worse than a bot that does not move.
- **A component hash alone is never enough to touch an element.** The rotation
  write indexes `block + inBlock*stride + yawOffset`, so a derived hash with an
  underived stride would write into the component block itself. All layout
  constants must be non-zero together (`rotationWritable()`), and reads are
  gated the same way (`botPlayerYaw` returns NaN rather than a wrong angle).
- Component hashes are FNV-1a 32 of the plain type name (`tools/entt_hash.py`
  reproduces them, including the negative result for `ItemInUseComponent`).
  When adding one, verify it against the SHA-pinned binary and record the hit
  count — a name that yields zero hits means the component is not instantiated
  under that name, and guessing variants is not a derivation.

- Every overlay window is a `TYPE_APPLICATION_PANEL` sub-window with
  `FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL`, and **no window is ever
  MATCH_PARENT**. That is the entire fix for "the game ignores input once the menu
  is open": a leaked panel can only ever eat its own ~320dp rectangle, never the
  screen.
- `EClientUi.attach()` **always rebuilds** — no early return. The framework tears
  sub-windows down on pause/rotation; holding stale Java references across that is
  how the overlay both vanished *and* kept swallowing game input.
- Close paths are explicit (✕ / chip / long-press panic). There is deliberately
  **no tap-outside-to-close**: that convenience is what forces a full-screen scrim
  back on top of the game.
- The 1 Hz watchdog is the self-heal: detached chip → re-add, detached panel →
  drop the reference. Keep it that way.
- Panel touch counter + last-touch coordinates stay on screen: diagnostics are
  field instrumentation until touch is confirmed stable on the device.
- `attach(Activity)` is invoked by name from the loader smali: its name and
  signature are ABI.

The ImGui-era rules below still apply if the GL overlay is ever switched back on:

- GUI draw must bind **FBO 0 + full-surface viewport** during
  `RenderDrawData` — in-world the game leaves off-screen FBOs (bloom/water/
  shadow passes) and reduced viewports bound at swap time; drawing into them
  renders the GUI into a texture nobody composites (menu visible on the title
  screen but invisible in a world/server).
- Rebuild ImGui GL objects if `eglGetCurrentContext()` changes (game recreates
  its render context on world joins).
- `g_frameMutex` try-lock per frame: transient swaps from a foreign thread get
  skipped rather than corrupting ImGui state.
- Menu-closed input: only gestures starting on the toggle-bar rect reach
  ImGui, **latched until finger-up** (else bar drags leak MOVEs into the game
  camera mid-drag).
- Menu-open input: all touches consumed (classic ClickGUI behavior).

Do NOT: rename/replace libminecraftpe.so, move hooks earlier than the poll
loop, swap Dobby for hand-rolled trampolines, or re-order steps 3.

## Loopback agent invariants (r36)

The agent is the in-game half of the companion-app link (`docs/MODULE_SDK.md`).
It is additive: the game boots and plays exactly as before with it disabled.

1. **No raw memory verbs. Ever.** `readMem`/`writeMem` must never exist. Any app
   on the device can reach a loopback port, so a generic memory verb would hand
   an arbitrary-write primitive into Minecraft to every other app on the phone.
   The agent speaks semantics (`attack`, `setRotation`, `setInput`, `getState`)
   and this side decides how to satisfy them with validated offsets.
2. **Token on every line.** The token is generated once, cached in
   `/sdcard/Download/eclient_agent.token` when storage permits, logged at INFO,
   and may be pinned via the `ECLIENT_AGENT_TOKEN` environment variable (host
   self-test + manual sessions). A line with a wrong token is rejected and the
   connection dropped.
3. **The bridge thread never touches game memory.** `publish()` latches a
   snapshot on the game thread (seqlock); the bridge only copies and serializes
   it. If this thread dies, the game must not notice — do not "optimize" it by
   reading memory directly from the socket handler.
4. **One scan per tick, and attacks index THAT scan.** `publish()` runs before
   `executeQueued()` in `onNormalTick`, so the actor order the app receives is
   the order `botAttackIndex()` indexes. Reordering these two calls silently
   makes attacks land on the wrong actor.
5. **`publish()` is gated on a live client.** With nobody connected it must stay
   a no-op: that is what makes the agent free when the app is not running.
6. **Capabilities are statements about what works, not wishes.** `caps()` feeds
   both the push and the refusal logic; a capability reported true while its
   layout is underived turns into a module that silently does nothing.
7. **The in-game UI is the only memory-capable GUI, so it is ON by default.**
   The companion app runs in a different process and cannot read or write
   Minecraft's memory — it only receives numbers the in-game agent already read.
   Anything that touches game memory (KillAura2's range/delay/rotation mode,
   Fly's mode, the patch-backed modules) therefore has to be drivable from
   inside the game, and `EClientUi` is that surface.
   * `attach(Landroid/app/Activity;)V` is called by name from the loader smali:
     never remove or rename it.
   * Hiding it is an opt-OUT: `/sdcard/Download/eclient_ui_off` (or
     `/sdcard/eclient_ui_off`), checked once per process. Do not turn the opt-in
     flag back on — that made the GUI look broken to anyone who had not created
     a file nobody told them about.
   * Agent-driven module control must keep working too (`listModules` /
     `toggleModule` / `setSetting`); those ops run on the GAME thread inside
     `executeQueued`, never on the bridge thread, because ModuleManager is not
     thread-safe against the tick.
   * Both GUIs write the same `ModuleManager`, which is fine because both run on
     the game/UI side of their own process — but never let a control write a
     value keyed by a stale module name: several modules share setting keys
     ("Mode" exists on both Fly and KillAura2), so the module name travels with
     each control instead of living in a shared "currently editing" field.
8. **Attacks are the game's own attack call**, reached through the same
   `attack(gameMode, actor, true)` path the auras use — not a new mechanism.

## Hard pins — change together or the pipeline aborts

- APK SHA-256 `69f6584c…3d59`: **3 copies** — `tools/build-and-patch.sh`,
  `tools/patch-minecraft-apk.sh`, `.github/workflows/patch_minecraft.yml`.
- Function addresses: `src/profile/Offsets_1_21_111.h` ONLY.
- Patch sites: `src/profile/PatchManager.cpp` — spec **name strings are the
  join key** to `ModuleManager` UI names; a mismatch silently kills that
  toggle. Composites share one name (see "NoBlur").
- Loader class/authority: smali + patcher + manifest edit:
  `com.rubidiumclient.host.EClientLoaderProvider` /
  `com.rubidiumclient.eclientloader`.
- Library name `eclient_host`: smali `loadLibrary` ↔ CMake target ↔ patcher.
- NDK 28.2.13676358 / ANDROID_PLATFORM android-26 / arm64-v8a /
  `-z max-page-size=16384`: CMake + workflow + scripts.

## Intentional quirks (do not "fix")

- **Debug build** in `tools/build-and-patch.sh`: the shipped, tested artifact.
  The workflow also does a Release pass first (double build — wasteful, safe).
- **`eclient_runtime` lib builds but is never injected** — launcher-era
  leftover (LoopbackBridge :38170 / NativeRuntimeBridge JNI). Dead in the
  APK flow by design ("everything inside MC").
- **Vendored And64InlineHook compiled but unused** — Dobby owns all hooks.
- **Fly plumbing is compiled but unreferenced**: `modules/FlyInput.*`, the
  `buttonUp`/`buttonDown` and `setFullBright` native exports, and the dormant
  `InGameGui.cpp` (which still asks `ModuleManager` about "Fly"/"MotionFly" —
  those names no longer exist, so it always answers false). Kept for ABI
  stability / cheap revival; nothing calls them.
- **`PatchManager` still validates all 18 sites** including the patches whose
  toggles were removed in r25. That is intentional: the validation is the
  fail-closed integrity gate, not a feature list.
- **`SYSTEM_ALERT_WINDOW` is declared and verified by the patcher** even when the
  overlay rung never runs. It is an optional third mount rung; the user grants it
  from the panel, never from a forced settings jump.
- **Shared debug keystore is COMMITTED** at `.signing/eclient-debug.p12`
  (PKCS12, alias `eclient`, android/android). Every builder signs identically
  → builds install over each other. It's a debug-grade sideload cert in a
  private repo by design; do not regenerate it unless you accept that all
  installs must then be uninstalled first. See docs/PATCHING_FROM_PHONE.md.

## Working rules

- Keep all gating: APK SHA gate, expected-byte fail-closed validation,
  resources.arsc STORED checks, `zipalign -P 16 4`, apksigner v1+v2+v3.
- GUI/input fixes must stay additive (see `gui/InGameGui.cpp`,
  `input/NativeInputHook.cpp`) — the game binaries must never be touched
  beyond the byte patches in PatchManager.
- The stock Minecraft APK release asset must only ever live on **private**
  repos (workflow downloads it via `gh release download --repo
  ${{ github.repository }}`).
- Menu diagnostics are field instrumentation: keep them until touch/drag is
  confirmed stable on device. r25 carries them in the panel status line
  (`touch #N chip #N last=(x,y)` + `patches: VALIDATED|NOT VALIDATED`) and logs
  every window add/remove and watchdog action through `EClientKotlin`.
