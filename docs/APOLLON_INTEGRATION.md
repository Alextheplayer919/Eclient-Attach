# Apollon integration: staging status

The requested product is an ARM64 E-Client patched Minecraft APK with the full
Apollon feature set. It is **not implemented or built yet**. The existing APK
pipeline still builds the old runtime; it must not be presented as the new
Apollon build.

`tools/build-apollon-native.sh` compiles the exact supplied `(3).zip` native
sources in isolation with ndk-build and NDK 28.2.13676358. It restricts the
build to arm64-v8a and requests 16 KiB maximum page alignment. It does not
modify the archive, package an APK, or integrate the resulting library. The
manual **Apollon native staging compile** workflow runs the same script.
Neither the workflow nor the generated `libReverseSigma.so` is a usable client
release. A successful compile does not establish compatibility with Minecraft.

The staging workflow also downloads the private, SHA-pinned Minecraft input and
runs `tools/check-apollon-target.py`: it checks the original bytes for the 18
existing host patch sites and whether the 27 Apollon ARM64 inline-hook targets
are in file-backed executable segments. It prints the hook bytes for further
analysis. Range checks alone do **not** establish that hook signatures or
behavior are correct, nor validate all Apollon patches or game field offsets.

## Required integration work

- Replace the old host internals without creating two `JNI_OnLoad` entry points
  or installing competing hooks; keep the existing provider and signing flow.
- Adapt Apollon's Platform2 `GLSurfaceView`/JNI renderer and touch routing to an
  in-process UI. Its frame routine clears the color buffer, which cannot be
  used unchanged on Minecraft's render surface. Preserve usable game input.
- Audit Apollon's ARM64 hooks and patches against the exact pinned APK. Its
  startup directly patches the native key handler and installs many hooks using
  hard-coded offsets; it does not implement the existing expected-byte
  fail-closed gate. Validate every write and hook before enabling it, and
  handle partial hook installation and shutdown safely.
- Adapt configuration storage and lifecycle, avoid concurrent access between
  render, input and game threads, and connect every menu feature and setting.
- Retain the APK SHA-256, alignment and signature checks in the existing
  patcher. Verify the resulting APK's loader, native ELF and signature, then
  test on the targeted Minecraft 1.21.111 ARM64 device before calling it a
  working build.

The archive contains other ABIs and a separate Android overlay application;
neither is part of the selected packaging. The source retains its upstream
credits; E-Client branding applies to the eventual product UI, not removal of
attribution.
