#!/usr/bin/env python3
"""Find swap action RTTI and constructor references in a pinned ARM64 ELF.

The outputs are static leads only; they must not be invoked based on this tool.
"""
import argparse
import mmap
import struct
import subprocess
from pathlib import Path

TERMS=(b'ItemStackRequestActionSwap',b'ItemStackRequestActionTake',b'ItemStackRequestActionPlace',b'ItemStackRequestActionTransfer')

def segments(data):
    phoff=struct.unpack_from('<Q',data,32)[0]; size,count=struct.unpack_from('<HH',data,54)
    if size<56 or count>1024 or phoff+size*count>len(data): raise ValueError('bad headers')
    out=[]
    for i in range(count):
        typ,flags,off,va,_,filesz,_,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*size)
        if typ==1 and off<=len(data) and filesz<=len(data)-off: out.append((off,va,filesz,flags))
    return out

def va_for_file(segments,pos):
    for off,va,size,_ in segments:
        if off<=pos<off+size: return va+pos-off
    return None

def type_names(data, load_segments):
    names={}
    for term in TERMS:
        at=0
        while True:
            at=data.find(term,at)
            if at<0: break
            start=data.rfind(b'\0',max(0,at-256),at)+1
            end=data.find(b'\0',at,min(len(data),at+256))
            if end>=0:
                name=data[start:end]
                # RTTI _ZTS names generally start with the length of the class.
                if name[:1].isdigit() and name[0:2].isdigit() and len(name)<160:
                    va=va_for_file(load_segments,start)
                    if va is not None: names[va]=name.decode('ascii','replace')
            at+=len(term)
    return names

def relative_matches(lib, values, destinations=None):
    result={}
    proc=subprocess.Popen(['readelf','-rW',str(lib)],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    for line in proc.stdout:
        cols=line.split()
        if len(cols)<4 or cols[2]!='R_AARCH64_RELATIVE': continue
        try: dest,addend=int(cols[0],16),int(cols[-1],16)
        except ValueError: continue
        if addend in values or (destinations is not None and dest in destinations): result.setdefault(addend,[]).append(dest)
    proc.stdout.close()
    if proc.wait(): raise RuntimeError('readelf failed')
    return result

def adrp_add_refs(data, loads, targets):
    by_page={}
    for addr in targets: by_page.setdefault(addr&~4095,set()).add(addr)
    refs={addr:[] for addr in targets}
    for off,va,size,flags in loads:
        if not flags&1: continue
        view=memoryview(data)[off:off+(size&~3)]
        try:
            for i,(insn,) in enumerate(struct.iter_unpack('<I',view)):
                if insn&0x9f000000!=0x90000000:continue
                reg=insn&31
                if reg==31:continue
                imm=(((insn>>5)&0x7ffff)<<2)|((insn>>29)&3)
                if imm&(1<<20):imm-=1<<21
                pc=va+i*4; page=(pc&~4095)+(imm<<12)
                if page not in by_page:continue
                for n in range(1,5):
                    if (i+n+1)*4>len(view):break
                    inst=struct.unpack_from('<I',view,(i+n)*4)[0]
                    if inst&0xff800000==0x91000000 and (inst>>5)&31==reg:
                        dest=page+(((inst>>10)&0xfff)<<(12 if inst&(1<<22) else 0))
                        if dest in refs: refs[dest].append((pc,n-1))
                        break
                    if (inst&0x7c000000==0x14000000 or inst&0xff000010==0x54000000 or
                        inst&0x7e000000 in (0x34000000,0x36000000) or
                        inst&0xfffffc1f in (0xd61f0000,0xd65f0000) or inst&31==reg):break
        finally:view.release()
    return refs

def main():
    p=argparse.ArgumentParser();p.add_argument('--lib',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--objdump',required=True);a=p.parse_args()
    with a.lib.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as mm:
        loads=segments(mm); names=type_names(mm,loads)
        # typeinfo second word references the RTTI name; vtable header's type
        # pointer is immediately before the vtable address point.
        to_names=relative_matches(a.lib,set(names)); types={dest-8:names[src] for src,dests in to_names.items() for dest in dests}
        # A derived type may have its name pointer omitted/merged or resolved
        # differently. Find RTTI objects deriving from the identified transfer
        # base: __si_class_type_info stores base typeinfo at offset 16.
        base_types={addr for addr,name in types.items() if 'TransferBase' in name}
        base_refs=relative_matches(a.lib,base_types)
        derived={dest-16 for src,dests in base_refs.items() for dest in dests}
        derived_names=relative_matches(a.lib,set(),{v+8 for v in derived})
        derived_name_map={dest-8:addend for addend,dests in derived_names.items() for dest in dests}
        for addr in derived:
            nameva=derived_name_map.get(addr,0)
            pos=next((off+(nameva-va) for off,va,size,_ in loads if va<=nameva<va+size),None)
            if pos is not None:
                end=mm.find(b'\0',pos,min(len(mm),pos+200))
                if end!=-1: types[addr]=mm[pos:end].decode('ascii','replace')+' (derived-candidate)'
        to_types=relative_matches(a.lib,set(types)); vtables={dest+8:types[src] for src,dests in to_types.items() for dest in dests}
        refs=adrp_add_refs(mm,loads,{**vtables, **names})
    lines=['Static leads only. RTTI/vtable references and function starts do not verify ABI, object lifetime or a safe swap.']
    for addr,name in sorted(names.items()):
        lines.append(f'RTTI {addr:#x} {name} code_refs={len(refs[addr])}')
        for pc,gap in refs[addr][:20]:
            lines.append(f'  name_xref={pc:#x} gap={gap}')
            dis=subprocess.check_output([a.objdump,'-d',f'--start-address={max(0,pc-48)}',f'--stop-address={pc+96}',str(a.lib)],text=True)
            lines.extend(x.strip() for x in dis.splitlines() if x.lstrip().startswith(tuple('0123456789abcdef')))

    lines+=['TYPEINFO '+hex(k)+' '+v for k,v in sorted(types.items())]
    for addr,name in sorted(vtables.items()):
        lines.append(f'VTABLE {addr:#x} {name} candidates={len(refs[addr])}')
        for pc,gap in refs[addr][:30]:
            lines.append(f'  xref={pc:#x} gap={gap}')
            dis=subprocess.check_output([a.objdump,'-d',f'--start-address={max(0,pc-24)}',f'--stop-address={pc+40}',str(a.lib)],text=True)
            lines.extend(x.strip() for x in dis.splitlines() if x.lstrip().startswith(tuple('0123456789abcdef')))
    a.output.write_text('\n'.join(lines)+'\n')
if __name__=='__main__':main()
