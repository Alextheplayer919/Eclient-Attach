#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INPUT_APK="${1:-}"
OUTPUT_APK="${2:-$ROOT/out/eclient-minecraft-1.21.111-arm64.apk}"

BUILD_DIR="$ROOT/build/native-android"

# CMake currently places the eclient_host target directly in BUILD_DIR.
HOST_SO="$BUILD_DIR/libeclient_host.so"

PATCHER="$ROOT/tools/patch-minecraft-apk.sh"

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

# ============================================================
# Validation
# ============================================================

if [[ -z "$INPUT_APK" ]]; then
    echo "ERROR: Missing Minecraft APK."
    echo
    echo "Usage:"
    echo "  $0 <minecraft-apk> [output-apk]"
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: Input APK does not exist:"
    echo "$INPUT_APK"
    exit 1
fi

if [[ ! -f "$PATCHER" ]]; then
    echo "ERROR: APK patcher does not exist:"
    echo "$PATCHER"
    exit 1
fi

chmod +x "$PATCHER"

mkdir -p "$ROOT/out"

# ============================================================
# Header
# ============================================================

echo
echo "=========================================="
echo " E-CLIENT BUILD + PATCH"
echo " Minecraft 1.21.111 ARM64"
echo "=========================================="
echo

# ============================================================
# 1. Verify original Minecraft APK
# ============================================================

echo "[1/4] Verifying Minecraft APK..."

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected:"
echo "$EXPECTED_SHA256"
echo
echo "Actual:"
echo "$ACTUAL_SHA256"
echo

if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
    echo "ERROR: Minecraft APK SHA-256 does not match."
    exit 1
fi

echo "Minecraft APK verified."

# ============================================================
# 2. Configure native build
# ============================================================

echo
echo "[2/4] Configuring native build..."

# The project previously used native/CMakeLists.txt.
# The current runtime uses native/runtime/CMakeLists.txt.
#
# Never reuse the old CMake cache because it can contain the
# previous source directory and cause:
#
#   CMake Error:
#   The source ".../native/runtime/CMakeLists.txt"
#   does not match the source ".../native/CMakeLists.txt"
#
# Therefore the native build directory is always recreated.

if [[ -d "$BUILD_DIR" ]]; then
    echo "Removing stale native CMake build directory:"
    echo "$BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# ------------------------------------------------------------
# Locate Android NDK
# ------------------------------------------------------------

NDK_PATH="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"

if [[ -z "$NDK_PATH" ]]; then
    echo "ERROR: Android NDK environment variable is not set."
    echo
    echo "Expected one of:"
    echo "  ANDROID_NDK_HOME"
    echo "  ANDROID_NDK_ROOT"
    exit 1
fi

if [[ ! -d "$NDK_PATH" ]]; then
    echo "ERROR: Android NDK directory does not exist:"
    echo "$NDK_PATH"
    exit 1
fi

TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake"

if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "ERROR: Android NDK CMake toolchain was not found:"
    echo "$TOOLCHAIN_FILE"
    exit 1
fi

echo "Native source:"
echo "$ROOT/native/runtime"
echo
echo "Native build:"
echo "$BUILD_DIR"
echo
echo "Android NDK:"
echo "$NDK_PATH"
echo

# ------------------------------------------------------------
# Configure CMake
# ------------------------------------------------------------

cmake \
    -S "$ROOT/native/runtime" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"

# ============================================================
# 3. Build native runtime
# ============================================================

echo
echo "[3/4] Building native runtime..."

cmake \
    --build "$BUILD_DIR" \
    --target eclient_host \
    --parallel

# ------------------------------------------------------------
# Verify CMake output
# ------------------------------------------------------------

if [[ ! -f "$HOST_SO" ]]; then
    echo
    echo "ERROR: Native host library was not produced at:"
    echo "$HOST_SO"
    echo
    echo "Searching for generated ARM64 shared libraries..."
    find "$BUILD_DIR" \
        -maxdepth 5 \
        -type f \
        \( -name '*.so' -o -name '*.a' \) \
        -print \
        || true
    exit 1
fi

echo
echo "Native host:"
ls -lh "$HOST_SO"

# ------------------------------------------------------------
# Verify native library architecture
# ------------------------------------------------------------

if ! command -v file >/dev/null 2>&1; then
    echo "ERROR: 'file' command is required."
    exit 1
fi

echo
echo "Checking native library architecture..."

FILE_OUTPUT="$(file "$HOST_SO")"

echo "$FILE_OUTPUT"

if ! echo "$FILE_OUTPUT" | grep -qiE 'ELF.*aarch64|ARM aarch64|ARM64'; then
    echo
    echo "ERROR: Native host library is not ARM64."
    exit 1
fi

echo
echo "ARM64 native library verified."

# ------------------------------------------------------------
# Verify native library is actually a shared object
# ------------------------------------------------------------

if ! echo "$FILE_OUTPUT" | grep -qi 'shared object'; then
    echo
    echo "ERROR: eclient_host is not an ELF shared object."
    exit 1
fi

# ------------------------------------------------------------
# Verify expected native filename
# ------------------------------------------------------------

if [[ "$(basename "$HOST_SO")" != "libeclient_host.so" ]]; then
    echo
    echo "ERROR: Unexpected native host filename:"
    echo "$(basename "$HOST_SO")"
    exit 1
fi

echo "Native host output verified."

# ============================================================
# 4. Patch Minecraft APK
# ============================================================

echo
echo "[4/4] Patching Minecraft APK..."

"$PATCHER" \
    "$INPUT_APK" \
    "$HOST_SO" \
    "$OUTPUT_APK"

# ============================================================
# Final verification
# ============================================================

if [[ ! -f "$OUTPUT_APK" ]]; then
    echo
    echo "ERROR: APK patcher completed without producing:"
    echo "$OUTPUT_APK"
    exit 1
fi

echo
echo "=========================================="
echo " BUILD COMPLETE"
echo "=========================================="
echo
echo "Output APK:"
ls -lh "$OUTPUT_APK"
echo
echo "SHA-256:"
sha256sum "$OUTPUT_APK"
echo
