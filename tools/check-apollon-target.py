#!/usr/bin/env python3
"""Inspect the pinned APK; executable-range checks are NOT hook validation."""
import argparse
import hashlib
import re
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_APK = '69f6584c000a4ec80ff8c4790f43d80b3e2ee67788842fdd39fd5be70fdb3d59'
SOURCE = ROOT / 'Apollon_Client_ImGui_v4.62_1.21.111 (3).zip'
PREFIX = 'Apollon_Client_ImGui/app/src/main/jni/'
# Existing host profile's expected original bytes, not Apollon's replacement bytes.
EXPECTED = {
    **{n: 'ffc300d1' for n in ('NoHurtCam', 'NoCamDistortion', 'NoBoatRotation',
        'NoCamSleep', 'PlaceCamera', 'SlowDownTriggers', 'NoSlowDown', 'WaterDrown',
        'LavaDrown', 'Noclip', 'XrayCameraThird')},
    'NoBlur1': 'ffc305d1', 'NoBlur2': 'ff0305d1',
    'VignetteRenderer': 'ff4302d1', 'LerpMotion': '080441f9',
    'FastBridge': 'fd7bbaa9fc6f01a9',
    'AutoClickMine': 'fd7bbea9', 'IsEmoting': '810b8052edcdec17',
    'FullBright': 'fd7bbfa9fd030091080040f9',
}

def inspect(apk):
    digest = hashlib.sha256()
    with open(apk, 'rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(block)
    if digest.hexdigest() != EXPECTED_APK:
        raise ValueError('APK SHA-256 mismatch: ' + digest.hexdigest())
    with zipfile.ZipFile(SOURCE) as z:
        offsets_text = z.read(PREFIX + 'offsets.h').decode('utf-8')
        source = z.read(PREFIX + 'main.cpp').decode('utf-8')
    aarch = offsets_text.split('namespace Fields64')[0]
    offsets = {k: int(v, 16) for k, v in re.findall(r'^\s*(\w+)\s*=\s*(0x[\da-fA-F]+)', aarch, re.M)}
    arm_source = source.split('void *hack_thread(void *)', 1)[1].split('#if defined(__aarch64__)', 1)[1].split('#elif', 1)[0]
    hooks = re.findall(r'A64HookFunction\s*\(\s*\(void\s*\*\)\s*getAbsoluteAddress\(MineLib,\s*Offsets64::(\w+)', arm_source)
    if len(hooks) != 27 or len(set(hooks)) != 27:
        raise ValueError(f'unexpected ARM64 hook list: {len(hooks)} entries')
    with zipfile.ZipFile(apk) as z:
        lib = z.read('lib/arm64-v8a/libminecraftpe.so')
    if lib[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Minecraft library is not little-endian ELF64')
    machine = struct.unpack_from('<H', lib, 18)[0]
    if machine != 183:
        raise ValueError(f'Minecraft library is not AArch64: e_machine={machine}')
    phoff = struct.unpack_from('<Q', lib, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', lib, 54)
    if phentsize < 56 or phoff + phentsize * phnum > len(lib):
        raise ValueError('invalid ELF program headers')
    segments = []
    for i in range(phnum):
        typ, flags, fileoff, vaddr, _, filesz, _, _ = struct.unpack_from('<IIQQQQQQ', lib, phoff + i * phentsize)
        if typ == 1 and flags & 1 and fileoff + filesz <= len(lib):
            segments.append((vaddr, vaddr + filesz, fileoff))
    def read_exec(addr, length):
        for start, end, fileoff in segments:
            if start <= addr and addr + length <= end:
                pos = fileoff + addr - start
                return lib[pos:pos + length]
        raise ValueError(f'0x{addr:x} is not in a file-backed executable PT_LOAD')
    failures = []
    swap_expected = bytes.fromhex('ff8301d1fd7b02a9fd830091f71b00f9')
    swap_actual = read_exec(0xcc9da9c, len(swap_expected))
    if swap_actual != swap_expected:
        raise ValueError('swap observer prologue mismatch: ' + swap_actual.hex())
    request_expected = bytes.fromhex('ff0303d1fd7b09a9fd430291f55300f9')
    request_actual = read_exec(0xcc93130, len(request_expected))
    if request_actual != request_expected:
        raise ValueError('request observer prologue mismatch: ' + request_actual.hex())
    print('Swap and request observer candidate prologues match; this does not validate ABI or behavior.')
    print('APK SHA-256 verified; library is ARM64 ELF.')
    for name, expected in EXPECTED.items():
        actual = read_exec(offsets[name], len(bytes.fromhex(expected))).hex()
        ok = actual == expected
        print(f'patch {name:20} 0x{offsets[name]:x}: {"MATCH" if ok else "MISMATCH"} ({actual}, expected {expected})')
        if not ok:
            failures.append(name)
    print('\nHook sites (range and bytes only; no signature or behavior validation):')
    for name in hooks:
        print(f'  {name:24} 0x{offsets[name]:x}  {read_exec(offsets[name], 16).hex()}')
    if failures:
        raise ValueError('expected-byte mismatch: ' + ', '.join(failures))
    print(f'\n{len(EXPECTED)} patch entries match; {len(hooks)} hook sites are in executable segments.')
    print('This does not validate function signatures, hooks, fields, GUI or device behavior.')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('apk', type=Path)
    try:
        inspect(p.parse_args().apk)
    except (ValueError, KeyError, OSError, zipfile.BadZipFile, struct.error) as e:
        print('ERROR:', e, file=sys.stderr)
        sys.exit(1)
