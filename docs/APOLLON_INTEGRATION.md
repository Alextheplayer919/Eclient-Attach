# Apollon integration: staging status

The experimental Apollon APK pipeline uses the pinned Minecraft APK, an adapted
Apollon native library and an in-process overlay. It is an **untested prototype**,
not a verified working release. The existing `build-and-patch.sh` pipeline still
builds the old runtime; it must not be presented as the Apollon build.

Use the **Build experimental Apollon E-Client APK** workflow or
`tools/build-apollon-apk.sh <pinned-minecraft.apk> [output.apk]`. It produces a
separately named APK, signed by the existing debug key. Device testing is still
needed for startup, menu visibility, touch routing, lifecycle and features. The
APK SHA gate and 18 original-byte checks are preserved; the latter are repeated
at runtime before Apollon's hooks are installed. The other hooks and field
layouts remain unverified. Apollon's original native key-handler patch is
intentionally disabled to avoid disabling Minecraft input.

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

- Verify the prototype's replacement host, provider, separate transparent
  `GLSurfaceView` and touch windows on an actual device. Its renderer clears
  its own surface, not Minecraft's; input and activity lifecycle behavior
  have not yet been verified.
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
