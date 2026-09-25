#!/usr/bin/env bash
# ============================================================================
# Builds kotlin-modules/src (plain Kotlin, no Gradle) into a smali tree that
# patch-minecraft-apk.sh injects as classes2.dex of the patched APK:
#
#   kotlinc -> classes.jar -> D8 -> classes.dex (renamed classes2.dex)
#
# The patcher injects the dex via apktool's unknown/ passthrough — APK-root
# classesN.dex files are picked up by ART's multidex loader verbatim, no
# baksmali round-trip needed.
#
# All toolchain pieces download on demand into kotlin-modules/build/tools.
# Run locally exactly the same as in CI. Needs: java 17+, curl, unzip.
#
# Output: kotlin-modules/build/classes2.dex (export KOTLIN_DEX_FILE for the
# patcher).
# ============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT/kotlin-modules/src"
BUILD_DIR="$ROOT/kotlin-modules/build"
TOOLS_DIR="$BUILD_DIR/tools"

KOTLIN_VERSION="${KOTLIN_VERSION:-2.0.21}"
R8_VERSION="${R8_VERSION:-8.5.35}"
SMALI_VERSION="${SMALI_VERSION:-2.5.2}"
# Compile-time API surface (guarded newer calls at runtime via SDK_INT checks;
# D8 --min-api 24 keeps the runtime floor).
ANDROID_PLATFORM_JAR="$TOOLS_DIR/android-33.jar"

mkdir -p "$TOOLS_DIR" "$BUILD_DIR"

download() {
    local url="$1" out="$2"
    if [[ ! -f "$out" ]]; then
        echo "Downloading $(basename "$out")..."
        curl -sSfL --retry 3 -o "$out" "$url"
    fi
}

# ── Kotlin compiler (standalone; also carries kotlin-stdlib.jar) ──────────
KOTLINC_HOME="$TOOLS_DIR/kotlinc-$KOTLIN_VERSION"
if [[ ! -x "$KOTLINC_HOME/bin/kotlinc" ]]; then
    download \
        "https://github.com/JetBrains/kotlin/releases/download/v${KOTLIN_VERSION}/kotlin-compiler-${KOTLIN_VERSION}.zip" \
        "$TOOLS_DIR/kotlin-compiler.zip"
    unzip -q -o "$TOOLS_DIR/kotlin-compiler.zip" -d "$TOOLS_DIR"
    mv "$TOOLS_DIR/kotlinc" "$KOTLINC_HOME"
fi
STDLIB_JAR="$KOTLINC_HOME/lib/kotlin-stdlib.jar"

# ── R8/D8 (Google's official dexer, standalone full jar; on Google's maven,
#    NOT Maven Central) ────────────────────────────────────────────────────
R8_JAR="$TOOLS_DIR/r8-$R8_VERSION.jar"
download \
    "https://dl.google.com/dl/android/maven2/com/android/tools/r8/${R8_VERSION}/r8-${R8_VERSION}.jar" \
    "$R8_JAR"

# ── android platform stubs (Sable mirror of the SDK platform jars) ────────
download \
    "https://github.com/Sable/android-platforms/raw/master/android-33/android.jar" \
    "$ANDROID_PLATFORM_JAR"

# ── Compile Kotlin ────────────────────────────────────────────────────────
CLASSES_JAR="$BUILD_DIR/classes.jar"
rm -f "$CLASSES_JAR"
echo "kotlinc: compiling $(find "$SRC_DIR" -name '*.kt' | wc -l) Kotlin files..."
mapfile -t KT_FILES < <(find "$SRC_DIR" -name '*.kt')
# android.jar on the classpath: the module layer uses Activities/Views now.
"$KOTLINC_HOME/bin/kotlinc" "${KT_FILES[@]}" -jvm-target 1.8 -nowarn \
    -classpath "$ANDROID_PLATFORM_JAR" -d "$CLASSES_JAR"

# ── Dex ───────────────────────────────────────────────────────────────────
DEX_OUT="$BUILD_DIR/dex"
rm -rf "$DEX_OUT"
mkdir -p "$DEX_OUT"
echo "D8: dexing..."
java -cp "$R8_JAR" com.android.tools.r8.D8 \
    --min-api 24 \
    --lib "$ANDROID_PLATFORM_JAR" \
    --output "$DEX_OUT" \
    "$CLASSES_JAR" "$STDLIB_JAR"

# ── Present as classes2.dex for apktool's unknown/ passthrough ────────────
CLASSES2="$BUILD_DIR/classes2.dex"
rm -f "$CLASSES2"
cp "$DEX_OUT/classes.dex" "$CLASSES2"

# Sanity: a real dex + the registry class inside it.
# (writes through intermediates so grep -q can't SIGPIPE pipefail us)
MAGIC=$(head -c 4 "$CLASSES2" || true)
[[ "$MAGIC" =~ ^dex ]] || { echo "ERROR: output is not a dex file."; exit 1; }
if command -v strings >/dev/null; then
    DEX_STRINGS=$(strings "$CLASSES2" || true)
    grep -q "rubidiumclient" <<< "$DEX_STRINGS" || {
        echo "ERROR: ModuleRegistry missing inside $CLASSES2"
        exit 1
    }
fi

echo
echo "Kotlin module layer ready: $CLASSES2"
echo "(export KOTLIN_DEX_FILE=\"$CLASSES2\" before running patch-minecraft-apk.sh)"
