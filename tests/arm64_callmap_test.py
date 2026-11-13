#!/usr/bin/env python3
import importlib.util
import struct
from pathlib import Path
p = Path(__file__).resolve().parents[1]/'tools'/'arm64-callmap.py'
s = importlib.util.spec_from_file_location('callmap', p)
m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
assert m.branch(0x94000002, 0x1000) == ('bl', 0x1008)
assert m.branch(0x17ffffff, 0x1000) == ('b', 0xffc)
assert m.branch(0xd65f03c0, 0x1000) is None
fdes = m.parse_fdes(['FDE cie=0 pc=00001000..00001010', 'FDE cie=0 pc=00001020..00001030'])
idx = m.RangeIndex(fdes)
assert idx.find(0x100f) == (0x1000, 0x1010) and idx.find(0x1010) is None
assert m.RangeIndex([(0x1000,0x1030),(0x1020,0x1040)]).find(0x1025) is None
# Minimal synthetic ELF: a forward BL, a backward B and an intra-FDE branch.
data = bytearray(0x140)
data[:6] = b'\x7fELF\x02\x01'
struct.pack_into('<H', data, 18, 183); struct.pack_into('<Q', data, 32, 64)
struct.pack_into('<HH', data, 54, 56, 1)
struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0x100, 0x1000, 0, 0x40, 0x40, 0x1000)
struct.pack_into('<I', data, 0x100, 0x94000008)
struct.pack_into('<I', data, 0x104, 0x14000001)
struct.pack_into('<I', data, 0x120, 0x17fffff8)
r = m.analyze(data, fdes, {'a':0x1004, 'b':0x1024, 'missing':0x2000}, 1)
assert r['a']['outgoing_total'] == 1 and r['a']['incoming_total'] == 1
assert r['a']['outgoing'][0]['target_is_fde_start']
assert r['b']['incoming_total'] == 1 and r['missing']['fde'] is None
assert m.analyze(data, fdes, {'a':0x1004}, 0)['a']['outgoing'] == []
try: m.segments(b'not elf')
except ValueError: pass
else: raise AssertionError('accepted malformed ELF')
print('ARM64 callmap tests passed')
