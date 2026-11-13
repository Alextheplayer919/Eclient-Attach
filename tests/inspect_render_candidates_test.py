#!/usr/bin/env python3
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools'/'inspect-render-candidates.py'
s=importlib.util.spec_from_file_location('render_inspector',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
lines=['0000 FDE cie=0 pc=00000000066e5900..00000000066e5b00','FDE cie=0 pc=00000000065f3600..00000000065f3700']
r=m.ranges(lines)
assert r['texture-item-diagnostic']==(0x66e5900,0x66e5b00)
assert r['terrain-atlas-load']==(0x65f3600,0x65f3700)
assert len(r)==2
print('Render candidate inspection tests passed')
