#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# E-CLIENT APK PATCHER
# Minecraft Bedrock 1.21.111 ARM64
#
# Arguments:
#
#   $1 = original Minecraft APK
#   $2 = E-Client native host library
#   $3 = output patched APK
#
# ============================================================

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INPUT_APK="${1:-}"
HOST_SO="${2:-}"
OUTPUT_APK="${3:-$ROOT/out/eclient-minecraft-1.21.111-arm64.apk}"

EXPECTED_SHA256="69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59"

WORK="$ROOT/.patch-work"
DECODED="$WORK/mc"

BUILT_APK="$WORK/eclient-unsigned.apk"
ALIGNED_APK="$WORK/eclient-aligned.apk"

LOADER_SOURCE="$ROOT/tools/minecraft-host-loader.smali"

LOADER_CLASS="com.rubidiumclient.host.EClientLoaderProvider"
LOADER_AUTHORITY="com.rubidiumclient.eclientloader"

echo "=========================================="
echo " E-CLIENT APK PATCHER"
echo " Minecraft Bedrock 1.21.111 ARM64"
echo "=========================================="
echo

# ============================================================
# Arguments
# ============================================================

if [[ -z "$INPUT_APK" ]]; then
    echo "ERROR: Missing input APK."
    echo
    echo "Usage:"
    echo "  $0 <minecraft.apk> <libeclient_host.so> <output.apk>"
    exit 1
fi

if [[ -z "$HOST_SO" ]]; then
    echo "ERROR: Missing native host library."
    echo
    echo "Usage:"
    echo "  $0 <minecraft.apk> <libeclient_host.so> <output.apk>"
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "ERROR: Input APK does not exist:"
    echo "$INPUT_APK"
    exit 1
fi

if [[ ! -f "$HOST_SO" ]]; then
    echo "ERROR: Native host library does not exist:"
    echo "$HOST_SO"
    exit 1
fi

if [[ ! -f "$LOADER_SOURCE" ]]; then
    echo "ERROR: Missing loader:"
    echo "$LOADER_SOURCE"
    exit 1
fi

# ============================================================
# 1. Check tools
# ============================================================

echo "[1/11] Checking required tools..."

for tool in \
    apktool \
    zipalign \
    apksigner \
    aapt2 \
    python3 \
    sha256sum \
    file \
    unzip \
    zip
do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: Required tool not found: $tool"
        exit 1
    fi
done

echo "Required tools available."
echo

# ============================================================
# 2. Verify original Minecraft APK
# ============================================================

echo "[2/11] Checking original Minecraft APK SHA-256..."

ACTUAL_SHA256="$(sha256sum "$INPUT_APK" | awk '{print $1}')"

echo "Expected:"
echo "$EXPECTED_SHA256"
echo

echo "Actual:"
echo "$ACTUAL_SHA256"
echo

if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
    echo "ERROR: Minecraft APK SHA-256 mismatch."
    exit 1
fi

echo "Minecraft APK SHA-256 verified."
echo

# ============================================================
# 3. Verify native library
# ============================================================

echo "[3/11] Checking E-Client native library..."

HOST_INFO="$(file "$HOST_SO")"

echo "$HOST_INFO"
echo

if ! printf '%s\n' "$HOST_INFO" | grep -Eq \
    'ELF 64-bit.*ARM aarch64|ELF 64-bit.*aarch64'; then

    echo "ERROR: Native host library is not ARM64."
    exit 1
fi

echo "ARM64 native library verified."
echo

echo "Loader:"
echo "$LOADER_SOURCE"
echo

echo "Native host:"
echo "$HOST_SO"
echo

# ============================================================
# 4. Prepare workspace
# ============================================================

echo "[4/11] Preparing workspace..."

rm -rf "$WORK"

mkdir -p "$WORK"
mkdir -p "$DECODED"

rm -f "$OUTPUT_APK"

echo "Workspace:"
echo "$WORK"
echo

# ============================================================
# 5. Decode APK
# ============================================================

echo "[5/11] Decoding Minecraft APK..."

apktool d \
    --force \
    "$INPUT_APK" \
    -o "$DECODED"

echo
echo "Minecraft APK decoded."
echo

# ============================================================
# 6. Configure resources
# ============================================================

echo "[6/11] Configuring APK resources..."

APKTOOL_YML="$DECODED/apktool.yml"

if [[ ! -f "$APKTOOL_YML" ]]; then
    echo "ERROR: apktool.yml not found."
    exit 1
fi

python3 - "$APKTOOL_YML" <<'PY'
import sys
from pathlib import Path
import re

path = Path(sys.argv[1])

text = path.read_text(encoding="utf-8")

pattern = r"(?m)^resourcesAreCompressed:\s*.*$"

if re.search(pattern, text):
    text = re.sub(
        pattern,
        "resourcesAreCompressed: false",
        text
    )
else:
    text += "\nresourcesAreCompressed: false\n"

path.write_text(
    text,
    encoding="utf-8"
)

print("resourcesAreCompressed: false")
PY

echo

# ============================================================
# Install loader
# ============================================================

echo "Installing E-Client loader..."

LOADER_DIR="$DECODED/smali/com/rubidiumclient/host"

mkdir -p "$LOADER_DIR"

cp \
    "$LOADER_SOURCE" \
    "$LOADER_DIR/EClientLoaderProvider.smali"

echo "Loader installed:"
echo "$LOADER_DIR/EClientLoaderProvider.smali"
echo

# ============================================================
# Install native library
# ============================================================

echo "Installing ARM64 native library..."

NATIVE_DIR="$DECODED/lib/arm64-v8a"

mkdir -p "$NATIVE_DIR"

cp \
    "$HOST_SO" \
    "$NATIVE_DIR/libeclient_host.so"

chmod 0644 \
    "$NATIVE_DIR/libeclient_host.so"

echo "Native library installed:"
echo "$NATIVE_DIR/libeclient_host.so"
echo

# ============================================================
# Update AndroidManifest.xml
# ============================================================

echo "Updating AndroidManifest.xml..."

MANIFEST="$DECODED/AndroidManifest.xml"

if [[ ! -f "$MANIFEST" ]]; then
    echo "ERROR: AndroidManifest.xml not found:"
    echo "$MANIFEST"
    exit 1
fi

python3 - "$MANIFEST" <<'PY'
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

manifest_path = Path(sys.argv[1])

ANDROID_NS = "http://schemas.android.com/apk/res/android"

ET.register_namespace(
    "android",
    ANDROID_NS
)

tree = ET.parse(manifest_path)
root = tree.getroot()

application = root.find("application")

if application is None:
    raise RuntimeError(
        "AndroidManifest.xml does not contain an application element"
    )

name_attr = f"{{{ANDROID_NS}}}name"
authority_attr = f"{{{ANDROID_NS}}}authorities"
exported_attr = f"{{{ANDROID_NS}}}exported"
init_order_attr = f"{{{ANDROID_NS}}}initOrder"

provider_name = "com.rubidiumclient.host.EClientLoaderProvider"
provider_authority = "com.rubidiumclient.eclientloader"

provider = None

for item in application.findall("provider"):
    if item.get(name_attr) == provider_name:
        provider = item
        break

if provider is None:
    provider = ET.Element("provider")
    application.append(provider)

provider.set(
    name_attr,
    provider_name
)

provider.set(
    authority_attr,
    provider_authority
)

provider.set(
    exported_attr,
    "false"
)

provider.set(
    init_order_attr,
    "100"
)

tree.write(
    manifest_path,
    encoding="utf-8",
    xml_declaration=True
)

print("E-Client loader provider added successfully.")
print(f"Provider: {provider_name}")
print(f"Authority: {provider_authority}")
PY

echo

# ============================================================
# Verify loader
# ============================================================

echo "Verifying loader installation..."

if [[ ! -f "$LOADER_DIR/EClientLoaderProvider.smali" ]]; then
    echo "ERROR: Loader installation failed."
    exit 1
fi

grep -q \
    'Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V' \
    "$LOADER_DIR/EClientLoaderProvider.smali" || {
        echo "ERROR: Loader does not call System.loadLibrary."
        exit 1
    }

grep -q \
    'const-string v0, "eclient_host"' \
    "$LOADER_DIR/EClientLoaderProvider.smali" || {
        echo "ERROR: Loader does not load eclient_host."
        exit 1
    }

echo "Loader verified."
echo

# ============================================================
# Verify manifest
# ============================================================

echo "Verifying manifest provider..."

python3 - "$MANIFEST" <<'PY'
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

manifest_path = Path(sys.argv[1])

ANDROID_NS = "http://schemas.android.com/apk/res/android"

tree = ET.parse(manifest_path)
root = tree.getroot()

application = root.find("application")

if application is None:
    raise RuntimeError(
        "No application element found"
    )

name_attr = f"{{{ANDROID_NS}}}name"
authority_attr = f"{{{ANDROID_NS}}}authorities"

expected_name = \
    "com.rubidiumclient.host.EClientLoaderProvider"

expected_authority = \
    "com.rubidiumclient.eclientloader"

found = False

for provider in application.findall("provider"):

    if (
        provider.get(name_attr) == expected_name
        and
        provider.get(authority_attr) == expected_authority
    ):
        found = True
        break

if not found:
    raise RuntimeError(
        "E-Client ContentProvider was not found"
        " in AndroidManifest.xml"
    )

print("Manifest provider verified.")
PY

echo

# ============================================================
# 7. Rebuild APK
# ============================================================

echo "[7/11] Rebuilding patched APK..."

rm -f "$BUILT_APK"

apktool b \
    "$DECODED" \
    -o "$BUILT_APK"

if [[ ! -f "$BUILT_APK" ]]; then
    echo "ERROR: Apktool did not create the APK."
    exit 1
fi

echo
echo "Unsigned APK created:"
ls -lh "$BUILT_APK"
echo

# ============================================================
# 8. Verify resources.arsc
# ============================================================

echo "[8/11] Checking resources.arsc compression..."

python3 - "$BUILT_APK" <<'PY'
import sys
import zipfile

apk = sys.argv[1]

with zipfile.ZipFile(apk, "r") as z:

    try:
        info = z.getinfo("resources.arsc")
    except KeyError:
        raise RuntimeError(
            "resources.arsc is missing from rebuilt APK"
        )

    if info.compress_type != zipfile.ZIP_STORED:
        raise RuntimeError(
            "resources.arsc is compressed. "
            "It must be uncompressed."
        )

    print("resources.arsc is uncompressed.")
PY

echo

# ============================================================
# Verify native library
# ============================================================

echo "Checking native library inside APK..."

if ! unzip -l "$BUILT_APK" | grep -q \
    'lib/arm64-v8a/libeclient_host.so'; then

    echo "ERROR: libeclient_host.so missing from APK."
    exit 1
fi

echo "Native library present."
echo

# ============================================================
# 9. Prepare signing key
# ============================================================

echo "[9/11] Preparing signing key..."

KEYSTORE="$WORK/eclient-debug.keystore"

if [[ ! -f "$KEYSTORE" ]]; then

    keytool \
        -genkeypair \
        -v \
        -keystore "$KEYSTORE" \
        -storepass android \
        -keypass android \
        -alias androiddebugkey \
        -keyalg RSA \
        -keysize 2048 \
        -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US"

fi

echo "Signing key ready."
echo

# ============================================================
# 10. zipalign
# ============================================================

echo "[10/11] Aligning APK..."

rm -f "$ALIGNED_APK"

zipalign \
    -P 16 \
    -f \
    4 \
    "$BUILT_APK" \
    "$ALIGNED_APK"

echo

echo "Checking APK alignment..."

zipalign \
    -c \
    -P 16 \
    -v \
    4 \
    "$ALIGNED_APK"

echo
echo "APK alignment verified."
echo

# ============================================================
# Sign APK
# ============================================================

echo "Signing APK..."

rm -f "$OUTPUT_APK"

apksigner sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --ks-key-alias androiddebugkey \
    --v1-signing-enabled true \
    --v2-signing-enabled true \
    --v3-signing-enabled true \
    --v4-signing-enabled false \
    --out "$OUTPUT_APK" \
    "$ALIGNED_APK"

if [[ ! -f "$OUTPUT_APK" ]]; then
    echo "ERROR: APK signing failed."
    exit 1
fi

echo

# ============================================================
# Verify APK signature
# ============================================================

echo "Verifying APK signature..."

apksigner verify \
    --verbose \
    --print-certs \
    "$OUTPUT_APK"

echo

# ============================================================
# 11. Final APK checks
# ============================================================

echo "[11/11] Performing final APK checks..."

python3 - "$OUTPUT_APK" <<'PY'
import sys
import zipfile

apk = sys.argv[1]

required = [
    "resources.arsc",
    "lib/arm64-v8a/libeclient_host.so",
]

with zipfile.ZipFile(apk, "r") as z:

    names = set(z.namelist())

    for item in required:
        if item not in names:
            raise RuntimeError(
                f"Missing required APK entry: {item}"
            )

    resources = z.getinfo("resources.arsc")

    if resources.compress_type != zipfile.ZIP_STORED:
        raise RuntimeError(
            "resources.arsc is compressed in final APK"
        )

print("Required APK entries verified.")
print("resources.arsc is uncompressed.")
PY

echo

# ============================================================
# Final result
# ============================================================

FINAL_SHA256="$(
    sha256sum "$OUTPUT_APK" | awk '{print $1}'
)"

echo "=========================================="
echo " E-CLIENT APK BUILD COMPLETE"
echo "=========================================="
echo
echo "Output:"
echo "$OUTPUT_APK"
echo
echo "Size:"
ls -lh "$OUTPUT_APK"
echo
echo "SHA-256:"
echo "$FINAL_SHA256"
echo
echo "Target:"
echo "Minecraft Bedrock 1.21.111 ARM64"
echo
echo "Native:"
echo "libeclient_host.so"
echo
echo "Loader:"
echo "$LOADER_CLASS"
echo
echo "Status:"
echo "APK patched, aligned, signed and verified."
echo
