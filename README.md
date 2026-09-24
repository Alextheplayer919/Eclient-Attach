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

## Current verified patch modules
NoHurtCam, FullBright, NoSlowDown, Noclip, AntiKnockback, NoBlur, NoCaveVignette, NoEmoteCooldown, NoWaterDrown, NoLavaDrown, NoCamDistortion, NoBoatRotation, NoCamSleep, PlaceCamera, SlowDownTriggers, XrayCameraThird, and AutoClickMine.

Speed, Fly, KillAura, CriticalHit, and ESP are registered as runtime modules but remain gated until their 1.21.111 function calling conventions and object layouts are verified. This prevents stale offsets from turning into a crash.
