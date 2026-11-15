# Apollon integration status

For current build instructions, signing caveats and a source map, see [BUILD_AND_CODE_GUIDE.md](BUILD_AND_CODE_GUIDE.md). The legacy `tools/build-and-patch.sh` and the unadapted `tools/build-apollon-native.sh` are not the current installable Apollon build. Use `tools/build-apollon-apk.sh` or the corresponding GitHub Actions workflow.

The current APK adapts the supplied Apollon native source, retains the pinned APK gate and injects `libeclient_host.so` through a non-exported ContentProvider. Its Java adapter runs ImGui on a separate translucent GL surface and supplies the draggable/collapsible menu. The native key-handler patch is disabled. Startup, menu, touch/lifecycle and Fly2 have received user device testing; not every upstream feature, hook, calling convention or field has been validated. The 19 expected-byte entries and 27 executable-range checks are safeguards, not an ABI or behavior proof. The adapter uses the existing gameplay/control code rather than replacing all of Apollon.

The schematic path currently includes an experimental two-block overlay. It loads vanilla resources, is lockable, and remains unverified on-device; it has no schematic file import, native depth buffer or full model rendering. See [the schematic README](../apollon-adapter/schematic/README.md) before describing it as a completed hologram.

`tools/build-apollon-native.sh` verifies and extracts the source ZIP and compiles the **unadapted** source for ARM64 with 16 KiB page alignment. Its staging library is not a patched APK or a validated injectable host. The separate mapper APK is documented in [mapper-app/README.md](../mapper-app/README.md).
