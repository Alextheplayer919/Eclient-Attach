#!/usr/bin/env python3
"""Wire music lifecycle only into the temporary special-edition Java copy."""
import shutil
import sys
from pathlib import Path
root = Path(__file__).resolve().parent.parent
java = Path(sys.argv[1])
overlay = java / 'com/rubidiumclient/host/ApollonOverlay.java'
text = overlay.read_text()
for old, new in (
    ('    @Override public void onActivityResumed(Activity a) {',
     '    @Override public void onActivityResumed(Activity a) {\n        SpecialMusic.resume(a);'),
    ('    @Override public void onActivityPaused(Activity a) {',
     '    @Override public void onActivityPaused(Activity a) {\n        SpecialMusic.pause(a);'),
    ('    @Override public void onActivityDestroyed(Activity a) {',
     '    @Override public void onActivityDestroyed(Activity a) {\n        SpecialMusic.pause(a);'),
):
    if text.count(old) != 1: raise ValueError('Unexpected lifecycle source: ' + old)
    text = text.replace(old, new, 1)
overlay.write_text(text)
shutil.copyfile(root / 'apollon-adapter/special/SpecialMusic.java', overlay.parent / 'SpecialMusic.java')
