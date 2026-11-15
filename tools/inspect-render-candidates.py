#!/usr/bin/env python3
"""Disassemble mapper xref windows and unwind entries; never invoke game code."""
import argparse
import re
import subprocess
from pathlib import Path

SITES = {
    'terrain-atlas-load': 0x65f36b8,
    'texture-item-diagnostic': 0x66e5a38,
    'block-tessellator-constructor-name': 0x675fcb4,
    'block-tessellator-color-name': 0x675fee8,
    'block-pipeline-name': 0x6765828,
    'terrain-json-second': 0x9fcaab8,
}

def ranges(lines):
    found={}
    pattern=re.compile(r'\bFDE\b.*?\bpc=([0-9a-fA-F]+)\.\.([0-9a-fA-F]+)')
    for line in lines:
        m=pattern.search(line)
        if not m:continue
        start,end=(int(x,16) for x in m.groups())
        for name,addr in SITES.items():
            if start<=addr<end:found[name]=(start,end)
    return found

def main():
    p=argparse.ArgumentParser();p.add_argument('--lib',type=Path,required=True);p.add_argument('--objdump',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    proc=subprocess.Popen(['readelf','--debug-dump=frames',str(a.lib)],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    bounds=ranges(proc.stdout);proc.stdout.close()
    if proc.wait():raise RuntimeError('unable to decode unwind metadata')
    a.output.mkdir(parents=True,exist_ok=True)
    for name,addr in SITES.items():
        r=bounds.get(name)
        windows=[('xref window', max(0,addr-128), addr+128)]
        if r: windows.insert(0,('unwind entry',r[0],min(r[0]+256,r[1])))
        lines=[f'{name}: xref={addr:#x} unwind={r}; static candidates only, not a callable ABI']
        for title,start,end in windows:
            lines.append('\n=== '+title+' ===')
            lines.append(subprocess.check_output([a.objdump,'-d',f'--start-address={start}',f'--stop-address={end}',str(a.lib)],text=True))
        (a.output/(name+'.txt')).write_text('\n'.join(lines))
if __name__=='__main__':main()
