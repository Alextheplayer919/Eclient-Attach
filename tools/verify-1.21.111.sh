#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# E-CLIENT 1.21.111 REFERENCE VERIFIER  (informational only)
#
# Inspects a supplied Minecraft APK and answers:
#   1. Is it the exact 1.21.111 ARM64 APK this runtime targets?
#   2. Is its libminecraftpe.so a 64-bit ARM64 ELF with an
#      executable PT_LOAD segment?
#   3. Does it expose a GNU build-id note? (This revision of the
#      library normally does NOT — that's expected, not a failure.)
#
# This script never modifies anything and is not part of the
# patch pipeline. All gating happens in patch-minecraft-apk.sh
# (exact APK SHA-256) and in PatchManager (per-site byte checks).
#
# Usage:
#   ./tools/verify-1.21.111.sh /path/to/minecraft.apk
# ============================================================

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

# Reference GNU build-id string observed for the matched library
# revision (kept for documentation; usually absent from the note
# section of the shipped binary):
REFERENCE_BUILD_ID="ea5614dcc3551721b14f9b4686aa1651c1b8f79e"

INPUT_APK="${1:-}"

if [[ -z "$INPUT_APK" ]]; then
    echo "Usage: $0 <minecraft.apk>"
    exit 2
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: APK not found: $INPUT_APK"
    exit 1
fi

echo "=========================================="
echo " E-CLIENT 1.21.111 REFERENCE VERIFIER"
echo "=========================================="
echo

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "APK SHA-256:"
echo "  expected: $EXPECTED_SHA256"
echo "  actual:   $ACTUAL_SHA256"

if [[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]]; then
    echo "  result:   MATCH — this is the targeted APK"
else
    echo "  result:   MISMATCH — patches stay fail-closed on this APK"
fi

echo

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

unzip -o -q "$INPUT_APK" "lib/arm64-v8a/libminecraftpe.so" -d "$WORK" || {
    echo "ERROR: lib/arm64-v8a/libminecraftpe.so not found in APK"
    exit 1
}

LIB="$WORK/lib/arm64-v8a/libminecraftpe.so"

echo "libminecraftpe.so:"
ls -lh "$LIB"
echo

FILE_INFO="$(file "$LIB")"
echo "ELF: $FILE_INFO"

if printf '%s\n' "$FILE_INFO" | grep -Eiq 'ELF 64-bit.*(aarch64|ARM aarch64)'; then
    echo "ARM64: yes"
else
    echo "ARM64: NO — not a supported binary"
fi

echo

echo "Program headers (executable PT_LOAD expected):"
if command -v llvm-readelf >/dev/null 2>&1; then
    llvm-readelf -lW "$LIB" | grep -E 'LOAD|Type' || true
    echo
    echo "GNU build-id note (absence is expected for this revision):"
    llvm-readelf -n "$LIB" 2>/dev/null | grep -i 'build' || echo "  (no build-id note found)"
elif command -v readelf >/dev/null 2>&1; then
    readelf -lW "$LIB" | grep -E 'LOAD|Type' || true
    echo
    echo "GNU build-id note (absence is expected for this revision):"
    readelf -n "$LIB" 2>/dev/null | grep -i 'build' || echo "  (no build-id note found)"
else
    echo "  (no readelf available — skipped)"
fi

echo
echo "Reference build-id string (from the matched revision):"
echo "  $REFERENCE_BUILD_ID"
echo
echo "Verifier finished — informational only, nothing was changed."
