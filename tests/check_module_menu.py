#!/usr/bin/env python3
"""Check migration of upstream module controls into the single-page menu."""
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
root = Path(__file__).resolve().parent.parent
archive = root / 'Apollon_Client_ImGui_v4.62_1.21.111 (3).zip'
prefix = 'Apollon_Client_ImGui/app/src/main/jni/'
menu = (root / 'apollon-adapter/menu/module_menu.h').read_text()
with tempfile.TemporaryDirectory() as tmp:
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name.startswith(prefix): z.extract(name, tmp)
        upstream = z.read(prefix + 'menu.h').decode()
    jni = Path(tmp) / prefix
    subprocess.run([sys.executable, str(root / 'tools/prepare-apollon-adapter.py'), str(jni)], check=True)
    transformed = (jni / 'menu.h').read_text()
    for category in (0, 1, 2, 4, 6):
        assert not re.search(r'case\s+' + str(category) + r'\s*:', transformed), category
    for category in (3, 5, 7): assert re.search(r'case\s+' + str(category) + r'\s*:', transformed), category
    assert '"Classic"' not in menu
    # Every upstream mob and ore option remains wired to the same variable.
    for group in ('Mob List', 'Xray'):
        if group == 'Mob List':
            section = upstream.split('BeginTabItem(OBFUSCATE("Mob List"))', 1)[1].split('ImGui::SetWindowFontScale(1.0)', 1)[0]
            names = re.findall(r'&\s*(no\w+)', section)
        else:
            section = upstream.split('BeginTabItem(OBFUSCATE("Xray"))', 1)[1].split('BeginTabItem(OBFUSCATE("Render"))', 1)[0]
            names = re.findall(r'&\s*(xray\w+)', section)
        assert names, group
        for name in names: assert re.search(r'&\s*' + re.escape(name) + r'\b', menu), (group, name)
print('Module migration checks passed')
