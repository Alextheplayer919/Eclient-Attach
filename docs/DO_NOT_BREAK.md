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

**r25 — the live UI is Android Views. ImGui/GL is dormant: `InGameGui::initialize()`
returns without installing the swap hook, so none of the ImGui code below runs.**

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
