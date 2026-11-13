#!/usr/bin/env python3
"""Offline, pinned ELF inspection; never calls game code or changes an APK."""
import argparse
import mmap
import struct
import re
import subprocess
from pathlib import Path

SITES = {
    'swap-owned-constructor': 0xcd7ceb8,
    'swap-action-context': 0xcd7fbbc,
    'swap-factory': 0xcc98d8c,
    'swap-constructor': 0xcc9db2c,
    'begin-request': 0xcc92740,
    'add-request-action': 0xcc93310,
    'selected-slot': 0xc5756e8,
    'selected-slot-index': 0xc5764dc,
    'offhand-container': 0xa2955b0,
    'use-item-on-block': 0xbaf96ec,
    'item-selected-event': 0xcd43638,
    'add-wrapper-1': 0xcca0dd4,
    'add-wrapper-2': 0xcca0fe0,
    'begin-wrapper': 0xcc92584,
    'action-context': 0xccd45ac,
    'action-constructor': 0xccd49e0,
    'legacy-action-construct-a': 0xcc6a094,
    'legacy-action-construct-b': 0xcc6a148,
    'inventory-ui-context': 0x58b542c,
    'manager-candidate-getter': 0x6bee850,
    'action-base-constructor': 0xcc9c240,
    'action-owner-accessor': 0xce0abf4,
    'action-slot-accessor': 0xce1a7c4,
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


def direct_bl_calls(data, base, targets):
    """Find AArch64 BL immediates in one executable PT_LOAD segment."""
    found = {addr: [] for addr in targets}
    usable = len(data) & ~3
    for index, (insn,) in enumerate(struct.iter_unpack('<I', data[:usable])):
        if insn & 0xfc000000 != 0x94000000:
            continue
        imm = insn & 0x3ffffff
        if imm & 0x2000000:
            imm -= 0x4000000
        pc = base + index*4
        target = pc + imm*4
        if target in found:
            found[target].append(pc)
    return found


def scan_callers(path, targets):
    with path.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as mm:
        if mm[:6] != b'\x7fELF\x02\x01':
            raise ValueError('expected ELF64 little endian')
        phoff = struct.unpack_from('<Q', mm, 32)[0]
        phsize, count = struct.unpack_from('<HH', mm, 54)
        result = {x: [] for x in targets}
        if phsize < 56 or count > 1024 or phoff + phsize*count > len(mm):
            raise ValueError('bad ELF program headers')
        for i in range(count):
            typ,flags,offset,va,_,size,_,_=struct.unpack_from('<IIQQQQQQ',mm,phoff+i*phsize)
            if typ != 1 or not flags & 1:
                continue
            if offset > len(mm) or size > len(mm)-offset:
                raise ValueError('bad executable segment')
            # memoryview avoids allocating a second copy of the game library.
            view=memoryview(mm)[offset:offset+size]
            try:
                part=direct_bl_calls(view,va,targets)
            finally:
                view.release()
            for key in result: result[key].extend(part[key])
        return result

def virtual_offset(data, address, size=1):
    phoff=struct.unpack_from('<Q', data, 32)[0]
    phsize,count=struct.unpack_from('<HH',data,54)
    if phsize<56 or count>1024 or phoff+phsize*count>len(data): raise ValueError('bad program headers')
    for i in range(count):
        typ,_,offset,va,_,filesz,_,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)
        if typ==1 and address>=va and address-va <= filesz and size<=filesz-(address-va):
            filepos=offset+(address-va)
            if filepos<=len(data) and size<=len(data)-filepos: return filepos
    return None


def relative_relocations(path, offsets):
    """Extract selected static R_AARCH64_RELATIVE addends from readelf."""
    result={}
    process=subprocess.Popen(['readelf','-rW',str(path)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,text=True)
    for line in process.stdout:
        cols=line.split()
        if len(cols)<4 or cols[2] != 'R_AARCH64_RELATIVE': continue
        try:
            dest=int(cols[0],16)
            if dest in offsets: result[dest]=int(cols[-1],16)
        except ValueError: continue
    process.stdout.close()
    if process.wait()!=0: return {}
    return result


def inspect_vtable(path, addresses):
    """Resolve selected Itanium ABI typeinfo using ELF relative relocations."""
    headers=relative_relocations(path, {v-8 for v in addresses.values()})
    name_offsets={value+8 for value in headers.values() if value}
    names=relative_relocations(path, name_offsets)
    lines=[]
    with path.open('rb') as file, mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as mm:
        for name,va in addresses.items():
            typeinfo=headers.get(va-8,0)
            nameva=names.get(typeinfo+8,0) if typeinfo else 0
            npos=virtual_offset(mm,nameva,1) if nameva else None
            if npos is None:
                lines.append(f'{name} vtable={va:#x} typeinfo={typeinfo:#x} name unavailable (relocation may be unsupported)')
                continue
            end=mm.find(b'\0',npos,min(len(mm),npos+256))
            if end<0: end=min(len(mm),npos+256)
            text=mm[npos:end].decode('ascii','replace')
            lines.append(f'{name} vtable={va:#x} typeinfo={typeinfo:#x} name_vaddr={nameva:#x} name={text}')
    return lines

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
    # Possible C++ vtable address points taken from constructor disassembly.
    # Values in the file may require loader relocations.
    vtables={'action-type-11':0x1037a820, 'legacy-type-15':0x103783f8, 'legacy-type-18':0x103783b0}
    (a.output / 'vtable-raw.txt').write_text('\n'.join(inspect_vtable(a.lib, vtables))+'\n')
    # Locate static callers of unwind-range starts, never assuming names or ABI.
    caller_names=('swap-owned-constructor','swap-action-context','swap-factory','swap-constructor','begin-request','add-request-action','add-wrapper-1','add-wrapper-2','begin-wrapper','action-context','action-constructor','legacy-action-construct-a','legacy-action-construct-b','inventory-ui-context','manager-candidate-getter','action-base-constructor','action-owner-accessor','action-slot-accessor')
    targets={ranges[k][0] for k in caller_names if k in ranges}
    calls=scan_callers(a.lib, targets)
    for name in caller_names:
        if name not in ranges:
            continue
        entry=ranges[name][0]
        positions=calls[entry]
        chunks=[f'{name} unwind_entry={entry:#x} direct_BL_count={len(positions)} (does not include indirect callers)']
        chunks.append('direct BL sites: ' + ', '.join(hex(p) for p in positions[:100]))
        for pc in positions[:8]:
            cmd=[a.objdump, '-d', f'--start-address={max(0,pc-64)}', f'--stop-address={pc+20}', str(a.lib)]
            chunks.append(subprocess.check_output(cmd,text=True))
        (a.output / (name+'-callers.txt')).write_text('\n'.join(chunks))

if __name__ == '__main__':
    main()
