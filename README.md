# E-Client Patch Runtime — Minecraft Bedrock 1.21.111 ARM64

This repository is the client patch/runtime portion of E-Client. The standalone Android launcher/application has been removed.

## Inputs
- Minecraft Bedrock 1.21.111 ARM64 APK supplied by the user.
- 1.21.111 offset/reference profile derived from the supplied Apollon source.

## Build and patch

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./tools/build-and-patch.sh /path/to/minecraft-1.21.111.apk ./eclient-minecraft-1.21.111-arm64.apk
```

The patcher validates the exact supplied APK SHA-256 before injecting the native host library. The native runtime validates the loaded `libminecraftpe.so` GNU build ID and checks each patch site's original ARM64 bytes before enabling a patch.

## What this build is (r25): a PvP-only client

Modules exposed in the in-game panel:

| Module | Kind | State |
|---|---|---|
| KillAura2 | native | works — target sort / range / delay / hurt-time; rotation modes wait on the rotation component |
| CriticalHit | native | registered, pending verification |
| AlwaysSprint | native | **rewritten** — only re-asserts sprint when no item is in use; inert until the item-use offset lands (it can no longer break eating) |
| AntiKnockback | memory patch | validated & working |
| NoSlowDown | memory patch | validated & working |
| Hitbox | Kotlin | pending: needs the entity size offset |
| AutoTotem | Kotlin | pending: needs inventory offsets |
| PvP Bot | Kotlin | **new in r27** — spacing, strafes, attack timing, learned patterns, auto-KillAura escalation (see `docs/PVP_BOT.md`) |
| ECSDump | native dev | hidden unless dev mode (long-press the panel title) |

Removed in r25 (code included, not just the UI entry): Fly, MotionFly, Speed, Step,
ESP, Noclip, NoWaterDrown, NoLavaDrown, SlowDownTriggers, NoEmoteCooldown,
AutoClickMine, and every visual patch (FullBright, NoHurtCam, NoBlur,
NoCaveVignette, NoCamDistortion, NoBoatRotation, NoCamSleep, PlaceCamera,
XrayCameraThird).

The other 16 byte patches are still **validated** at boot — that validation is the
integrity gate for the binary being patched and is deliberately untouched — they
are simply no longer exposed as toggles.

Anything whose memory is not derived yet fails **inert and says so in the UI**.
Nothing is faked, and nothing half-finished is allowed to silently misbehave.

### PvP Bot (r27)

A per-tick decision system, not a script: it watches every nearby player
(position, velocity, health, i-frames), holds a fighting distance, opens every
engagement by backing off (people charge straight in), rates each movement
pattern ("WASD combination") per situation, forces a different pattern after a
miss streak, and turns KillAura2 on by itself when it starts taking non-stop
hits. Scores persist across restarts. Turning the player toward the target is
the one thing still waiting on the rotation component — it shows the aim error
instead of pretending. Full design: `docs/PVP_BOT.md`.

## UI and input (r26)

The overlay is Android Views: a draggable chip and a small panel that is **never
full-screen and never window-modal**, so any touch that does not land on a widget
belongs to Minecraft. The decor-view `OnTouchListener` is not installed — nothing of
ours sits in the game's own input path — while `nativeTouchReady()` is still called
because it is the KotlinBridge bind point.

### Nothing fails silently any more

r25 shipped a structurally correct overlay that showed nothing on a real device, and
every way it could fail looked identical from the outside. r26 makes each layer
announce itself:

| Signal | What it proves |
|---|---|
| Native toast `E-Client r26: native ready` | `libeclient_host.so` loaded, JNI + provider lifecycle fine |
| Kotlin toast `E-Client r26: menu up (…)` | the injected `classes6.dex` runs and the chip mounted |
| Chip label `E-Client r26 · PANEL ○` | which mount mechanism won, plus a live 1 Hz heartbeat |

Missing signal 1 → the native host never ran. Signal 1 but not 2 → the injected dex
never ran (packaging/classloader), not the UI. Both but no chip → the window layer
only, and the log file below names the exact exception.

### Mount ladder

The chip is mounted before anything else that can fail, walking three rungs and
reporting the winner in the chip label:

1. `PANEL` — `TYPE_APPLICATION_PANEL` sub-window on the activity token (input passes
   through; this is what r24 proved on device),
2. `INJECTED` — a child view of the game's own content frame. Needs no token, no
   permission and no z-order; the game's `SurfaceView` composites behind its window's
   views, so this cannot be hidden and cannot eat input outside its own rectangle,
3. `OVERLAY` — `TYPE_APPLICATION_OVERLAY`, needs *Display over other apps*
   (declared in the patched manifest; a one-tap **GRANT OVERLAY** row appears in the
   panel when it is not granted, and it is never required).

### Log file (no adb needed)

Every mount attempt with its exception, watchdog event, touch count and module
toggle is written to `Download/EClient/eclient_ui_<epoch>.txt` (MediaStore, so the
phone's Files app can open it) and mirrored into the app's external files dir. When
something misbehaves, that file is the bug report.
