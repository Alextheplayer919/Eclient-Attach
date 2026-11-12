#!/usr/bin/env bash
# Compile the supplied Apollon native source in isolation. This is NOT an
# injectable E-Client library: its JNI bootstrap and UI need an adapter first.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIP="$ROOT/Apollon_Client_ImGui_v4.62_1.21.111 (3).zip"
EXPECTED=593318306a96ae907d3cf35734b751966c45fdb93c515420394bd0f374cce40a
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$NDK" || ! -x "$NDK/ndk-build" ]]; then
    echo 'Set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) to an installed Android NDK.' >&2
    exit 1
fi
if [[ ! -f "$ZIP" ]] || [[ "$(sha256sum "$ZIP" | cut -d' ' -f1)" != "$EXPECTED" ]]; then
    echo 'Apollon source archive is missing or does not match the expected SHA-256.' >&2
    exit 1
fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# Only extract the native source, never the archive's stale generated build output.
unzip -q "$ZIP" 'Apollon_Client_ImGui/app/src/main/jni/*' -d "$WORK"
JNI="$WORK/Apollon_Client_ImGui/app/src/main/jni"
test -f "$JNI/Android.mk"
# Insert this before BUILD_SHARED_LIBRARY so ndk-build sees it for the module.
sed -i '/^include $(BUILD_SHARED_LIBRARY)/i LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384' "$JNI/Android.mk"
"$NDK/ndk-build" \
    NDK_PROJECT_PATH="$WORK" \
    APP_BUILD_SCRIPT="$JNI/Android.mk" \
    NDK_APPLICATION_MK="$JNI/Application.mk" \
    APP_ABI=arm64-v8a \
    NDK_LIBS_OUT="$WORK/libs" NDK_OUT="$WORK/obj" \
    -j"$(getconf _NPROCESSORS_ONLN)"
LIB="$WORK/libs/arm64-v8a/libReverseSigma.so"
test -s "$LIB"
OUT="$ROOT/build/apollon-native-staging"
mkdir -p "$OUT"
cp "$LIB" "$OUT/libReverseSigma.so"
echo "Native compile output: $OUT/libReverseSigma.so"
echo 'Staging only: do not inject this unadapted library into Minecraft.'
