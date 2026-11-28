import os, subprocess, sys, tempfile, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
archive=root/'Apollon_Client_ImGui_v4.62_1.21.111 (3).zip'
prefix='Apollon_Client_ImGui/app/src/main/jni/'
with tempfile.TemporaryDirectory() as tmp:
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name.startswith(prefix):z.extract(name,tmp)
    jni=Path(tmp)/prefix
    env=dict(os.environ,ECLIENT_COMBAT_LAB='1')
    subprocess.run([sys.executable,str(root/'tools/prepare-apollon-adapter.py'),str(jni)],check=True,env=env)
    main=(jni/'main.cpp').read_text();menu=(jni/'module_menu.h').read_text()
    assert 'eclient_combat_lab::install()' in main
    assert 'eclient_combat_lab::tick(thiz, myGameMode, playerPos, actors)' in main
    assert 'eclient_combat_lab::cancelIfArmed()' in main
    assert 'eclient_fake::tick(thiz, myLevel, myBlockSource, playerPos, actors)' in main
    assert 'eclient_gatyo::tick(thiz, myLevel, myGameMode, myBlockSource, playerPos, CI, eclientHasFakeTarget ? &eclientFakeTarget : nullptr)' in main
    assert 'eclient_fake::draw(draw_list' in (jni/'menu.h').read_text()
    assert (jni/'gatyo_combat.h').exists()
    assert '"Combat Lab"' not in menu and '"Crystal Aura"' in menu and '"Auto Totem"' in menu
    assert 'eclient_combat_lab::crystalSettings()' in menu and 'eclient_combat_lab::totemSettings()' in menu and (jni/'combat_lab.h').exists()
    transformed=(jni/'menu.h').read_text()
    assert 'ImGui::Button("Console")' in transformed and 'case 8: eclient_combat_lab::drawConsole();' in transformed
    assert (jni/'live_console.h').exists()
print('Combat lab opt-in preparation passed')
