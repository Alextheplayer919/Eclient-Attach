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
    main = (jni / 'main.cpp').read_text()
    # Each resize starts from the current context's base style, not a static
    # first-use flag or already scaled sizes.
    assert 'eclientBaseStyle = ImGui::GetStyle();' in main
    assert 'style = eclientBaseStyle;' in main
    assert 'std::max(style.ScrollbarSize, 18.0f)' in main
    assert 'const eclient::combat::PlacementArgs args{a0,a1,a2,a3,a4,a5,a6}' in main
    assert 'eclient::combat::forwardPlacement(old_GameMode_FastBridge, args)' in main
    assert 'int (*old_GameMode_FastBridge)(int a1' not in main
    assert 'eclient_crystal_break_test::tick(player, gameReady);' in main
    assert 'ImGui::TreeNode("Crystal break test")' in menu
    assert 'ImGui::TreeNode("Inventory swap trace")' in menu
    assert 'eclient_combat_diagnostics::swapConstruction(first, second)' in main
    assert 'getAbsoluteAddress(MineLib, 0xcc9da9c)' in main
    assert 'getAbsoluteAddress(MineLib, 0xcc93130)' in main
    assert 'eclient_combat_diagnostics::requestAction(actionOwner)' in main
    assert 'return old_RequestAction(manager, actionOwner);' in main
    assert 'memcmp((const void*)swapAddress, swapExpected' in main
    assert (jni / 'crystal_break_test.h').exists()
    assert (jni / 'practice_target.h').exists()
    assert 'eclient_practice_target::tick(player, gameReady);' in main
    assert 'eclient_practice_target::render(draw_list, CameraPos, currentFov, currentViewMatrix);' in transformed
    assert '"Practice target"' in menu
    assert '"Crystal Aura"' in menu
    assert 'eclient_crystal_aura::tick(player, gameReady);' in main
    assert (jni / 'crystal_aura_break.h').exists()
    resize = main.split('Platform2_resize(JNIEnv* env, jobject obj, jint width, jint height) {', 1)[1].split('void Patches()', 1)[0]
    assert 'static bool isLoaded' not in resize
    assert 'ImGui::PopItemWidth();' in transformed
    assert transformed.count('Based on Apollon source code by ZEFF.') == 1
    overlay = (root / 'apollon-adapter/java/com/rubidiumclient/host/ApollonOverlay.java').read_text()
    assert 'Apollon source code by ZEFF' not in overlay
    for category in (0, 1, 2, 4, 6):
        assert not re.search(r'case\s+' + str(category) + r'\s*:', transformed), category
    for category in (3, 5, 7): assert re.search(r'case\s+' + str(category) + r'\s*:', transformed), category
    assert '"Classic"' not in menu
    # Module rows contain modules, not options or diagnostics; moved controls
    # remain available independently of the parent module's enabled state.
    rows = menu.split('static Module movement[]', 1)[1].split('static int selected', 1)[0]
    for option in ('Target Lock', 'Xray Caves', 'Xray Camera', 'Crystal test', 'Focused test', 'Crystal break test'):
        assert '"' + option + '"' not in rows, option
    assert 'ImGui::Checkbox("Target Lock", &targetLock)' in menu
    assert 'ImGui::Checkbox("Xray Caves", &xrayCaves)' in menu
    assert 'ImGui::Checkbox("Xray Camera (Third Person)", &xrayCam)' in menu
    for name in ('Crystal test', 'Focused test', 'Crystal break test'):
        assert 'ImGui::TreeNode("' + name + '")' in menu
    assert 'Shared with KillAura and MobAura' in menu

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
