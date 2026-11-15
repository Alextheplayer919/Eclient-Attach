# E-Client — Minecraft Bedrock 1.21.111 ARM64

E-Client is an experimental patched Minecraft APK with an in-process Apollon-based client, a draggable ClickGUI, Fly2, and a separate offline Android mapper. The supported target is the **SHA-256-pinned Android ARM64 1.21.111 APK**, not a generic Minecraft version. The mapper is a separate APK and never replaces Minecraft.

## Start here

- [Build, architecture and source guide](docs/BUILD_AND_CODE_GUIDE.md) — prerequisites, exact build commands, signing and code map.
- [Complete source and archive inventory](docs/SOURCE_REFERENCE.md) — tracked source/configuration and archive members; bundled upstream code is retained as supplied.
- [Schematic and preview status](apollon-adapter/schematic/README.md) — renderer research, experimental two-block preview and limitations.
- [Fly2](apollon-adapter/fly/README.md) and [standalone mapper](mapper-app/README.md).
- [Retired combat research](docs/CRYSTAL_AURA_RESEARCH_ARCHIVE.md) — historical research only; Crystal Aura and special music are not part of the current client.

## Build the current client

The [Build experimental Apollon E-Client APK workflow](.github/workflows/apollon-experimental.yml) builds the current client and uploads `eclient-apollon-experimental-arm64`. It requires access to the repository's pinned Minecraft input release; the Minecraft APK is **not** included in this checkout. For local setup and signing caveats, see the build guide.

```bash
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/28.2.13676358"
export PATH="$ANDROID_SDK_ROOT/build-tools/35.0.0:$PATH"
./tools/build-apollon-apk.sh /path/to/Minecraft-1.21.111-arm64-v8a.apk
```

The resulting APK is `out/eclient-apollon-experimental-arm64.apk`. The old `tools/build-and-patch.sh` builds a **different, legacy runtime**; it does not build the current Apollon client. The Apollon native staging script also does not produce an installable client.

## Status and safety

The patched client and GUI have been exercised on a device, but not every upstream hook, feature, field layout or lifecycle is verified. Fly2 may trigger server corrections. The schematic preview is an **experimental, unverified two-block overlay**, not schematic import or a finished hologram. It does not have Minecraft's depth buffer. Back up worlds before uninstalling Minecraft: these debug APKs are re-signed, and generated keys change between builds.

The original sources and bundled third-party code remain attributed in their archives and licenses. This project is not affiliated with Mojang or Microsoft.
