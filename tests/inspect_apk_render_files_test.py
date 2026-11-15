import importlib.util
import tempfile
import zipfile
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/inspect-apk-render-files.py'
s=importlib.util.spec_from_file_location('apkfiles',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
with tempfile.TemporaryDirectory() as d:
    apk=Path(d)/'test.apk'
    with zipfile.ZipFile(apk,'w') as z:
        z.writestr('lib/arm64-v8a/libdummy.so',b'not an ELF')
        z.writestr('assets/shaders/test.vert','void main(){}')
        z.writestr('assets/textures/foo.png','x')
        z.writestr('lib/x86/ignored.so','x')
    r=m.inspect(apk)
    assert r[0]=='ARM64 shared libraries: 1'
    assert 'libdummy.so' in r[1] and 'none/unknown' in r[1]
    assert 'Graphics-related asset paths: 2; shader/material path candidates: 1' in r
    assert 'shader asset: assets/shaders/test.vert' in r
assert m.NEEDED.findall('0x000 (NEEDED) Shared library: [libEGL.so]') == ['libEGL.so']
print('APK render inventory tests passed')
