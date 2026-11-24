#!/usr/bin/env python3
"""Compare a small set of extractor vtable candidates with ELF relocations.

Static comparison only; never uses names as proof of a function's identity/ABI.
Requires ijson and the separately unpacked extractor JSON; never writes the JSON.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import ijson

PINNED = '65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1'
CLASSES = ('GameMode', 'PlayerInventory', 'Inventory', 'OffhandContainerModel',
           'ContainerTransferClient', 'ItemStackRequestActionSwap', 'ItemStackRequestActionPlace', 'EndCrystalItem')

def integer(value):
    if isinstance(value, int): return value
    if not isinstance(value, str): raise ValueError('missing address')
    return int(value, 0)

def select(path):
    with path.open('rb') as f:
        source = next(ijson.items(f, 'source'), None)
    if not source or source.get('librarySha256') != PINNED or source.get('architecture') != 'AArch64':
        raise ValueError('extractor metadata does not identify the pinned ARM64 library')
    chosen = {}
    with path.open('rb') as f:
        for name, info in ijson.kvitems(f, 'classes'):
            if name in CLASSES:
                if name in chosen: raise ValueError('duplicate class')
                chosen[name] = info
    if set(chosen) != set(CLASSES): raise ValueError('some selected classes are missing')
    return chosen

def targets(classes):
    positions = {}
    for name, info in classes.items():
        base = integer(info['vtable'])
        for slot in info['slots']:
            num = integer(slot['slot'])
            if num < 0 or num > 1024: raise ValueError('invalid vtable slot')
            # Extractor vtable points to the Itanium ABI header; +16 skips
            # offset-to-top and typeinfo, then indexes virtual function slots.
            addr = base + 16 + 8*num
            positions.setdefault(addr, []).append((name, num, integer(slot['address'])))
    return positions

def read_relocations(lines, positions):
    result = {}
    for line in lines:
        parts = line.split()
        if len(parts) < 4: continue
        try: addr = int(parts[0],16)
        except ValueError: continue
        if addr not in positions: continue
        kind=parts[2]
        # Only RELATIVE relocations can be resolved as base + addend without
        # symbol lookup. Other types remain explicitly unresolved.
        try:
            addend=int(parts[-1],16) if kind == 'R_AARCH64_RELATIVE' else None
        except ValueError: addend=None
        result[addr] = (kind, addend)
    return result

def compare(classes, relocations):
    positions=targets(classes); result={name: {'match':0,'different':0,'unresolved':0,'slots':[]} for name in classes}
    for where, entries in positions.items():
        for name,num,expected in entries:
            kind,actual=relocations.get(where,('not found',None))
            verdict='unresolved' if actual is None else ('match' if actual==expected else 'different')
            row={'slot':num,'vtable_slot_vaddr':hex(where),'extractor_target':hex(expected),'relocation':kind,
                 'elf_addend':hex(actual) if actual is not None else None,'result':verdict}
            result[name][verdict]+=1;result[name]['slots'].append(row)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--json',required=True,type=Path);p.add_argument('--lib',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    with a.lib.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
    if digest != PINNED: raise ValueError('library SHA-256 mismatch')
    classes=select(a.json); positions=targets(classes)
    proc=subprocess.Popen(['readelf','-rW',str(a.lib)],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    relocs=read_relocations(proc.stdout,positions);proc.stdout.close()
    if proc.wait(): raise RuntimeError('readelf relocation inspection failed')
    result=compare(classes,relocs)
    report={'library_sha256':digest,'classes':result,'limitations':[
        'Matching relocation addends establish only static pointer values, not function names, semantics or ABI.',
        'Only AArch64 RELATIVE relocations decoded by readelf are resolved; missing/packed/other relocations remain unresolved.',
        'Slot mapping assumes the supplied class vtable denotes an Itanium header with 16-byte header.',
        'No functions are invoked and no server behavior, object lifetimes or inventory synchronization is established.']}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    for name, entry in result.items():
        print(f'::notice title=Vtable cross-check::{name}: match={entry["match"]}, different={entry["different"]}, unresolved={entry["unresolved"]}')

if __name__=='__main__':main()
