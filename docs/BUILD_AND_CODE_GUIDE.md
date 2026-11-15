# Build, architecture and source guide

This describes the repository's current Apollon-based APK and the separate mapper. It is a guide to the code and build inputs, not a claim that all code paths or third-party binaries were audited. [SOURCE_REFERENCE.md](SOURCE_REFERENCE.md) inventories the individual tracked files and archive contents. See [APOLLON_INTEGRATION.md](APOLLON_INTEGRATION.md) for historical integration notes; where it describes the early prototype as entirely untested, the current state below supersedes it.

## Build inputs and prerequisites

- Linux with Bash, Python 3, `unzip`, `zip`, `sha256sum`, `file`, `find`, `getconf`, and a C++17 host compiler for tests.
- JDK 17, Android SDK platform 35, Android build-tools 35.0.0 (including `d8`, `aapt2`, `zipalign`, `apksigner`), and Android NDK 28.2.13676358. Put the build-tools directory on `PATH`, set `ANDROID_SDK_ROOT` and `ANDROID_NDK_HOME`.
- apktool 2.10.0 on `PATH`; CI installs the release jar from the upstream GitHub release. Internet access and licenses are required to install SDK/NDK tools.
- The exact original `Minecraft-1.21.111-arm64-v8a.apk`, obtained separately with appropriate rights. Expected SHA-256: `69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59`. The CI workflow fetches it from the repository release `minecraft-input`; this requires release access. Do not commit or distribute the input APK as part of this PR.
- `Apollon_Client_ImGui_v4.62_1.21.111 (3).zip` from this checkout. The build verifies its SHA-256 (`593318306a96ae907d3cf35734b751966c45fdb93c515420394bd0f374cce40a`). Its native source, bundled ImGui, GLM, JSON, hooks and static libraries are extracted temporarily; the archive is not edited.

The target library SHA-256 is `65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1`. A matching version label is not a substitute for matching bytes. The input SHA gate rejects different APKs even if the native library matches.

## Current APK build

```bash
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/28.2.13676358"
export PATH="$ANDROID_SDK_ROOT/build-tools/35.0.0:$PATH"
./tools/build-apollon-apk.sh /path/to/Minecraft-1.21.111-arm64-v8a.apk
# Optional second argument overrides the output path.
```

The script checks the pinned target, extracts the source archive, applies `prepare-apollon-adapter.py` to a temporary copy, builds ARM64 `libeclient_host.so`, verifies required JNI exports, compiles adapter Java with platform 35, generates dex and invokes the patcher. The patcher decodes the original APK, installs the provider loader and native host, appends adapter dex without overwriting existing dex, rebuilds resources, aligns, signs and verifies the output. The generated APK defaults to `out/eclient-apollon-experimental-arm64.apk`. The private Minecraft input and generated APKs are not prerequisites to be checked into Git.

**Signing:** `patch-minecraft-apk.sh` deletes its `.patch-work` directory at the start, then generates a debug key there. Consequently each fresh build can have a different signature. It cannot update the official Minecraft app or a differently signed earlier build without uninstalling it; uninstalling can delete local worlds and data. Back up worlds first. The checked-in historical `.signing/eclient-debug.p12` is not the key used by this script. Do not treat generated debug signing as a production signing strategy.

The existing `.github/workflows/apollon-experimental.yml` performs the above on GitHub Actions and uploads the named artifact. CI validates APK structure, JNI exports, signature and selected patch bytes; this is not proof that every hook, field offset, graphics behavior or module is safe on a device. The patcher removes its `.patch-work` directory and the destination output file before rebuilding; never point the output to a file you need to keep.

## Mapper and legacy builds

`tools/build-mapper-apk.sh [output.apk]` creates a **separate** `com.eclient.mapper` APK using platform 35 and build-tools on `PATH`; it needs `aapt`, `d8`, `zipalign`, `apksigner`, `keytool`, `javac`, `jar`, and `zip`. Default output is `out/eclient-mapper.apk`. Its signing key is ephemeral too. `.github/workflows/mapper-apk.yml` tests the Java analyzer, builds it and runs an offline check on the pinned library.

`tools/build-and-patch.sh` and `native/runtime/` implement the older Fortnite-derived runtime, not the current Apollon client. `tools/build-apollon-native.sh` compiles unadapted upstream Apollon as a staging library only. Do not confuse either result with the installable Apollon APK.

## Runtime architecture and code map

1. `apollon-adapter/loader.smali` is the injected, non-exported `ContentProvider` entrypoint. It loads `libeclient_host.so` and starts the Java overlay integration. `tools/patch-minecraft-apk.sh` installs it and adds the manifest entry.
2. `tools/prepare-apollon-adapter.py` applies checked replacements to the upstream native sources in a temporary directory; failure to find the expected source text aborts the build. It installs the adapter files, adjusts `Android.mk`, replaces the unsafe FastBridge forwarder and checks expected patch bytes. `tools/check-apollon-target.py` checks APK identity, patch bytes and executable hook ranges; ranges and bytes do not validate calling conventions.
3. `apollon-adapter/java/com/rubidiumclient/host/ApollonOverlay.java` manages the application/activity overlay, touch windows and Fly2 log publication. `java/com/mojang/minecraftpe/platforms/Platform2.java` owns a separate transparent `GLSurfaceView`, serializes native ImGui calls and supplies assets. `tools/check-apollon-jni.py` validates linked JNI symbols.
4. `apollon-adapter/menu/module_menu.h` organizes upstream module toggles and settings into selectable horizontal pages. It does not replace upstream modules. `compat/placement_forward.h` preserves the raw ARM64 arguments/return value of a placement forwarding hook; its argument semantics and synthetic invocation are not established.
5. `apollon-adapter/fly/`: `fly2.cpp/.h` implement the core flight calculation; `fly2_adapter.h` connects it to the client; `fly2_calculated.h`, `fly2_limits.h` and `fly2_probe.h` contain lifecycle, limits and candidate input reading. Fly2 is separate from other fly modules. Calculated-mode inputs are experimental; normal game controls are intended. On Android 10+ the app attempts to overwrite a single `Downloads/fly2test.txt`; see its README for limitations.
6. `apollon-adapter/schematic/mcstructure.*` reads bounded uncompressed little-endian Bedrock structures and preserves two layers; `anchor.h` models a lockable world anchor; `texture_catalog.*` maps block faces to resource paths; `cube_mesh.*` builds a bounded, provisional stone/dirt/grass mesh. `preview_adapter.h` connects a disabled-by-default fixed two-block example to the overlay and Android assets. It is **not** structure import, full block models, Java format conversion, native depth-tested rendering or a device-validated hologram. The standalone parser is not currently wired to the test preview.
7. `mapper-app/` is an independent Android application. `MainActivity.java` handles document selection, background work and report export; `ElfMapper.java` maps ELF ranges and bounded string/ADRP+ADD candidates; `ReportStore.java` manages its saved report. Mapper reports are read-only heuristics, not proof of usable offsets.
8. `tools/arm64-callmap.py`, `inspect-render-candidates.py`, `inspect-apk-render-files.py` and `tools/ghidra/RenderCandidates.java` perform offline candidate, ELF branch, asset and targeted decompilation analysis. `ghidra-render.yml` runs Ghidra headlessly; addresses and pseudocode are leads, not validated render hooks. The other workflows test and inspect code, package the separate mapper, or build the current/legacy paths.
9. `tests/` contains isolated host tests for Fly2, module migration, structure parsing, anchors, texture lookups, cube mesh, placement forwarding, APK inventories and ARM64 candidate decoding. `mapper-app/tests/` tests its Java components. Resource tests extract the nlohmann JSON header from the tracked Apollon source archive; the APK fixture additionally requires the separately supplied pinned APK.
10. `native/vendor/` and the contents of the source ZIPs are upstream or bundled code and binaries. They are not rewritten for documentation; see their embedded notices/licenses and [SOURCE_REFERENCE.md](SOURCE_REFERENCE.md) for file-level inventory. Archives other than the pinned `(3).zip` are historical/reference inputs, not required by the current Apollon build.

## Tests and CI

Run the inexpensive host checks directly:

```bash
python3 tests/check_module_menu.py
python3 tests/arm64_callmap_test.py
python3 tests/inspect_render_candidates_test.py
python3 tests/inspect_apk_render_files_test.py
g++ -std=c++17 -Wall -Wextra -Werror tests/mcstructure_test.cpp apollon-adapter/schematic/mcstructure.cpp -o /tmp/mcstructure-test && /tmp/mcstructure-test
g++ -std=c++17 -Wall -Wextra -Werror tests/schematic_anchor_test.cpp -o /tmp/anchor-test && /tmp/anchor-test
```

The resource/mesh tests and their JSON header extraction are spelled out in `.github/workflows/schematic-core.yml`. The real-asset test and ARM64 cross-compilation are in `.github/workflows/render-native-inspection.yml`. The mapper host test commands are in `mapper-app/README.md`. The Ghidra workflow requires downloading Ghidra and the pinned APK and does not build an APK. Full Android validation requires the SDK, NDK, pinned input and target device; CI cannot validate camera offsets or actual in-world rendering.
