#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT}"
ANDROID_JAR="$ANDROID_SDK_ROOT/platforms/android-35/android.jar"
OUTPUT="${1:-$ROOT/out/eclient-mapper.apk}"
for tool in aapt d8 zipalign apksigner keytool javac jar zip; do command -v "$tool" >/dev/null; done
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes" "$WORK/dex" "$(dirname "$OUTPUT")"
mapfile -d '' JAVA < <(find "$ROOT/mapper-app/src" -name '*.java' -print0)
javac -source 8 -target 8 -classpath "$ANDROID_JAR" -d "$WORK/classes" "${JAVA[@]}"
(cd "$WORK/classes" && jar cf "$WORK/classes.jar" .)
d8 --min-api 26 --output "$WORK/dex" "$WORK/classes.jar"
aapt package -f -M "$ROOT/mapper-app/AndroidManifest.xml" -I "$ANDROID_JAR" -F "$WORK/unsigned.apk"
(cd "$WORK/dex" && zip -q "$WORK/unsigned.apk" classes.dex)
zipalign -f 4 "$WORK/unsigned.apk" "$WORK/aligned.apk"
keytool -genkeypair -noprompt -keystore "$WORK/debug.keystore" -storepass android -keypass android -alias mapper -dname 'CN=E-Client Mapper Debug' -keyalg RSA -keysize 2048 -validity 3650 >/dev/null 2>&1
apksigner sign --ks "$WORK/debug.keystore" --ks-pass pass:android --key-pass pass:android --out "$OUTPUT" "$WORK/aligned.apk"
apksigner verify --verbose "$OUTPUT"
echo "Built $OUTPUT (ephemeral debug signing key; uninstall prior builds before updating)."
