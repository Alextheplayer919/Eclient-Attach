# E-Client Patch Runtime — Minecraft Bedrock 1.21.111 ARM64

This repository is the client patch/runtime portion of E-Client. The standalone Android launcher/application has been removed.

## Baseline

Started from **Fortnite19-main** (`Fortnite19-main (1) (1).zip` in this repo) — the minimal native host that injects `libeclient_host.so` into `libminecraftpe.so` and exposes an ImGui ClickGUI + byte-patch toggles.

## Minecraft-only progress kept

On top of that baseline, the following **Minecraft verification** work has been retained (everything else outside the zip was scrapped):

- **`native/runtime/src/profile/Offsets_1_21_111.h`** — expanded from ~30 to 311 lines: `GetAbilities` `0x0CDF943C`, `GetLayer` `0x0CDF9450`, `SetAbility` `0x0CDF87E0`, `VelocityField` `0x18`, `EcsRegistryField` `0x10`/`EntityIdField` `0x18`, 6 derived ECS hashes (`ActorRotation` `0x75df36b7`, `MoveInput` `0x018b1887`, … via `tools/entt_hash.py`), full Apollon table with field offsets, and the negative `ItemInUseComponent` result.
- **`PatchManager`** — `NoWaterDrown`/`NoLavaDrown` rename fix, composite `NoBlur` (2 sites, one toggle), `hasSpec()`/`specCount()`, `set(..., why)` with per-site error reporting.
- **`NativeMemory` / `GameBridge` / `LoopbackBridge` / `MinecraftHost`** — SHA-verified `libminecraftpe.so` validation (executable-range + fingerprint, not the absent GNU build-id), `patchCoverageAudit()` wiring check at boot, `entt_hash.py` offline verification.
- **Tools** — `tools/verify-1.21.111.sh`, `tools/entt_hash.py` kept; `patch-minecraft-apk.sh` + `minecraft-host-loader.smali` reverted to the non-Kotlin Fortnite versions (no dex injection, no `SYSTEM_ALERT_WINDOW`).
- **Docs** — `docs/APOLLON_RECOVERED.md`, `DO_NOT_BREAK.md`, `PATCHING_FROM_PHONE.md`, `PERF_AUDIT.md` kept; `PVP_BOT.md`/`MODULE_SDK.md`/`MERGE_PLAN.md`/`PROXY_MERGE_FEASIBILITY.md` scrapped.

## What was scrapped (outside the zip)

`kotlin-modules/` (Hitbox, AutoTotem, PvpBot, ModuleRegistry, EClientUi), `native/runtime/src/kotlin/KotlinBridge.*`, `bridge/AgentState.*, DebugFiles.*`, `memory/ComponentWalk.*`, `modules/FlyInput.*, MelodyModules.*`, the Kotlin Views overlay (`build-kotlin-modules.sh`, `minecraft-touch-listener.smali`, touch ABI, `ModuleRegistry.init()`), and `native/runtime/tests/host/` (bench/fake_world/clickgui checks). The repo now has **no Kotlin** and no module implementation beyond Fortnite's 23-entry `ModuleManager`.

## Current verified patch modules (Fortnite baseline)

NoHurtCam, FullBright, NoSlowDown, Noclip, AntiKnockback, NoBlur, NoCaveVignette, NoEmoteCooldown, NoWaterDrown, NoLavaDrown, NoCamDistortion, NoBoatRotation, NoCamSleep, PlaceCamera, SlowDownTriggers, XrayCameraThird, AutoClickMine. Speed, Fly, KillAura, CriticalHit, ESP remain gated until their calling conventions are verified.

## Build and patch

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./tools/build-and-patch.sh /path/to/minecraft-1.21.111.apk ./eclient-minecraft-1.21.111-arm64.apk
```

The patcher validates the exact APK SHA-256 before injecting. The native runtime validates `libminecraftpe.so` and each patch site's original bytes before enabling a patch.

## Sources

- `Fortnite19-main (1) (1).zip` — baseline
- `Apollon_Client_ImGui_v4.62_1.21.111 (2).zip` — 1.21.111 reference profile used to derive the offsets above
