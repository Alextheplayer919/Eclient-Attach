# Patching tools

`build-and-patch.sh` builds the native ARM64 host with the Android NDK and injects it into the supplied Minecraft 1.21.111 APK.

`patch-minecraft-apk.sh` performs the APK-side loader injection and signs the resulting APK with a local debug keystore.

The scripts intentionally do not contain a copy of Minecraft. The user supplies the APK.
