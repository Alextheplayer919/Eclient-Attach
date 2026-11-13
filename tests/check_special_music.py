#!/usr/bin/env python3
"""Verify special wiring is isolated and lifecycle-bound."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
source = root / 'apollon-adapter/java'
original = (source/'com/rubidiumclient/host/ApollonOverlay.java').read_text()
assert 'SpecialMusic.' not in original
music = (root/'apollon-adapter/special/SpecialMusic.java').read_text()
assert 'p.setLooping(true)' in music
assert 'getAssets().open(ASSET)' in music
assert 'MediaPlayer' in music
assert 'player.pause()' in music
assert (root/'e.mp3').stat().st_size > 0
with tempfile.TemporaryDirectory() as tmp:
    java = Path(tmp)/'java'
    shutil.copytree(source, java)
    subprocess.run([sys.executable, str(root/'tools/prepare-special-music.py'), str(java)], check=True)
    overlay=(java/'com/rubidiumclient/host/ApollonOverlay.java').read_text()
    assert overlay.count('SpecialMusic.resume(a);') == 1
    assert overlay.count('SpecialMusic.pause(a);') == 2
    assert (java/'com/rubidiumclient/host/SpecialMusic.java').read_text() == music
print('Special-edition music wiring checks passed')
