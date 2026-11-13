#!/usr/bin/env python3
"""Check exported JNI names for the Java overlay before packaging the APK."""
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: check-apollon-jni.py <llvm-nm> <libeclient_host.so>')
nm, library = sys.argv[1:]
output = subprocess.check_output([nm, '-D', '--defined-only', library], text=True)
exports = {line.split()[-1] for line in output.splitlines() if line.split()}
prefix = 'Java_com_mojang_minecraftpe_platforms_Platform2_'
required = {'JNI_OnLoad'} | {prefix + name for name in (
    'setFly2LogPath', 'init', 'resize', 'step', 'imgui_1Shutdown', 'MotionEventClick', 'getWindowRect')}
missing = sorted(required - exports)
if missing:
    raise SystemExit('Missing exported JNI symbols: ' + ', '.join(missing))
print('Verified JNI exports: ' + ', '.join(sorted(required)))
