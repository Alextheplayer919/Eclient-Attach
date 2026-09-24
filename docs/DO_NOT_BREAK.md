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
3. Bootstrap loop polls `dl_iterate_phdr` (250 ms) until **unmodified**
   `libminecraftpe.so` appears → `GameBridge` sanity profile →
   `PatchManager` fail-closed validation of all 18 byte sites →
   `GameplayModules` Dobby hooks (NormalTick, SpeedHack) →
   `InGameGui` (Dobby: eglSwapBuffers ± eglCreateWindowSurface) →
   `NativeInputHook` (Dobby: nativeKeyHandler, AInputQueue_getEvent).

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
- Menu diagnostics (`Patch profile:`, `Input: key/motion/touches=`) are field
  instrumentation: keep them until touch/drag is confirmed stable on device.
