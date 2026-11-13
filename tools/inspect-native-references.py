#!/usr/bin/env python3
"""Offline, pinned ELF inspection; never calls game code or changes an APK."""
import argparse
import re
import subprocess
from pathlib import Path

SITES = {
    'begin-request': 0xcc92740,
    'add-request-action': 0xcc93310,
    'selected-slot': 0xc5756e8,
    'selected-slot-index': 0xc5764dc,
    'offhand-container': 0xa2955b0,
    'use-item-on-block': 0xbaf96ec,
}


def unwind_ranges(lines, sites):
    # GNU readelf --debug-dump=frames reports FDE address ranges. A range is
    # unwind metadata, not by itself evidence of a safe callable interface.
    result = {}
    pattern = re.compile(r'\bFDE\b.*?\bpc=([0-9a-fA-F]+)\.\.([0-9a-fA-F]+)')
    for line in lines:
        m = pattern.search(line)
        if not m:
            continue
        start, end = (int(x, 16) for x in m.groups())
        for name, addr in sites.items():
            if start <= addr < end:
                result[name] = (start, end)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--lib', required=True, type=Path)
    p.add_argument('--objdump', required=True)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    process = subprocess.Popen(['readelf', '--debug-dump=frames', str(a.lib)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    ranges = unwind_ranges(process.stdout, SITES)
    process.stdout.close()
    if process.wait() != 0:
        print('::warning::Unwind decoding failed; no inferred function boundaries.')
    a.output.mkdir(parents=True, exist_ok=True)
    for name, addr in SITES.items():
        bounds = ranges.get(name)
        windows = [('xref', max(0, addr-128), addr+128)]
        if bounds:
            windows.insert(0, ('unwind-entry', bounds[0], min(bounds[0]+256, bounds[1])))
        text = [f'{name} xref={addr:#x} unwind_range={bounds}; candidate evidence, not a callable signature.']
        for label, start, end in windows:
            cmd = [a.objdump, '-d', f'--start-address={start}', f'--stop-address={end}', str(a.lib)]
            text.extend([f'\n=== {label} ===', subprocess.check_output(cmd, text=True)])
        (a.output / (name+'.txt')).write_text('\n'.join(text))

if __name__ == '__main__':
    main()
