#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APK="${1:?Usage: build-apollon-apk.sh input.apk [output.apk]}"
OUTPUT="${2:-$ROOT/out/eclient-apollon-experimental-arm64.apk}"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT}"
[[ -x "$NDK/ndk-build" ]] || { echo 'Android NDK not found' >&2; exit 1; }
command -v d8 >/dev/null
python3 "$ROOT/tools/check-apollon-target.py" "$APK"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
ZIP="$ROOT/Apollon_Client_ImGui_v4.62_1.21.111 (3).zip"
echo '593318306a96ae907d3cf35734b751966c45fdb93c515420394bd0f374cce40a  '"$ZIP" | sha256sum -c -
unzip -q "$ZIP" 'Apollon_Client_ImGui/app/src/main/jni/*' -d "$WORK"
JNI="$WORK/Apollon_Client_ImGui/app/src/main/jni"
python3 "$ROOT/tools/prepare-apollon-adapter.py" "$JNI"
"$NDK/ndk-build" NDK_PROJECT_PATH="$WORK" APP_BUILD_SCRIPT="$JNI/Android.mk" \
 NDK_APPLICATION_MK="$JNI/Application.mk" APP_ABI=arm64-v8a \
 NDK_LIBS_OUT="$WORK/libs" NDK_OUT="$WORK/obj" -j"$(getconf _NPROCESSORS_ONLN)"
mkdir -p "$WORK/classes" "$WORK/dex" "$(dirname "$OUTPUT")"
mapfile -d '' JAVA_FILES < <(find "$ROOT/apollon-adapter/java" -name '*.java' -print0)
javac -source 8 -target 8 -classpath "$ANDROID_SDK_ROOT/platforms/android-35/android.jar" \
 -d "$WORK/classes" "${JAVA_FILES[@]}"
(cd "$WORK/classes" && jar cf "$WORK/adapter.jar" .)
d8 --min-api 26 --output "$WORK/dex" "$WORK/adapter.jar"
ECLIENT_LOADER_SOURCE="$ROOT/apollon-adapter/loader.smali" ECLIENT_EXTRA_DEX="$WORK/dex/classes.dex" \
 bash "$ROOT/tools/patch-minecraft-apk.sh" "$APK" "$WORK/libs/arm64-v8a/libeclient_host.so" "$OUTPUT"
