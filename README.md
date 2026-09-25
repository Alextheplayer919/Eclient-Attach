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

## UI and input (r25)

The overlay is Android Views on `WindowManager` sub-windows: a draggable chip and a
small panel that is **never full-screen and never window-modal**, so any touch that
does not land on a widget belongs to Minecraft. The decor-view `OnTouchListener` is
no longer installed — nothing of ours sits in the game's own input path — while
`nativeTouchReady()` is still called because it is the KotlinBridge bind point.
A 1 Hz watchdog re-adds a chip the framework tore down and drops a panel that is no
longer attached.
