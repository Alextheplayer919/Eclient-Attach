#!/usr/bin/env python3
"""Inventory APK ARM64 native dependencies and rendering-related assets, offline."""
import argparse
import hashlib
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path

NEEDED = re.compile(r'\(NEEDED\).*?\[(.*?)\]')
GRAPHICS = re.compile(r'(egl|gles|vulkan|graphics|render|shader|mesh|texture)', re.I)

def inspect(apk):
    out = []
    with zipfile.ZipFile(apk) as z:
        libs = sorted(n for n in z.namelist() if n.startswith('lib/arm64-v8a/') and n.endswith('.so') and not n.endswith('/'))
        out.append(f'ARM64 shared libraries: {len(libs)}')
        for n in libs:
            info=z.getinfo(n)
            # Stream large entries to avoid retaining a second copy in memory.
            digest=hashlib.sha256()
            with tempfile.NamedTemporaryFile(suffix='.so') as f, z.open(n) as src:
                while True:
                    chunk=src.read(1024*1024)
                    if not chunk: break
                    digest.update(chunk); f.write(chunk)
                f.flush()
                p=subprocess.run(['readelf','-dW',f.name],capture_output=True,text=True)
            deps=NEEDED.findall(p.stdout) if p.returncode==0 else []
            out.append(f'{n} size={info.file_size} sha256={digest.hexdigest()} NEEDED={",".join(deps) or "none/unknown"}')
        assets=sorted(n for n in z.namelist() if n.startswith('assets/') and not n.endswith('/') and GRAPHICS.search(n))
        shaders=[n for n in assets if re.search(r'(shader|\.vert$|\.frag$|\.glsl$|\.material$)',n,re.I)]
        out.append(f'Graphics-related asset paths: {len(assets)}; shader/material path candidates: {len(shaders)}')
        out.extend('shader asset: '+n for n in shaders[:100])
        if len(shaders)>100: out.append(f'shader paths omitted: {len(shaders)-100}')
    return out

def main():
    p=argparse.ArgumentParser(); p.add_argument('apk',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    lines=inspect(a.apk);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text('\n'.join(lines)+'\n')
    for line in lines:
        if line.startswith(('ARM64 shared','lib/arm64','Graphics-related')):
            print('::notice title=APK render file inventory::'+line.replace('%','%25'))

if __name__=='__main__':main()
