#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct

p=Path(__file__).resolve().parents[1]/'tools'/'find-inventory-action-types.py'
spec=importlib.util.spec_from_file_location('inventory_types',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
# One executable load segment with an encoded type name and ADRP/ADD pair.
data=bytearray(4096)
struct.pack_into('<Q',data,32,64);struct.pack_into('<HH',data,54,56,1)
struct.pack_into('<IIQQQQQQ',data,64,1,5,0,0x1000,0,4096,4096,4096)
name=b'26ItemStackRequestActionSwap\0';data[0x200:0x200+len(name)]=name
loads=m.segments(data)
assert m.type_names(data,loads)=={0x1200:'26ItemStackRequestActionSwap'}
# ADRP x0, current page; ADD x1, x0, #0x200.
struct.pack_into('<II',data,0x100,0x90000000,0x91080001)
assert m.adrp_add_refs(data,loads,{0x1200})[0x1200]==[(0x1100,0)]
struct.pack_into('<I',data,0x104,0x14000000) # branch must stop a scan
assert m.adrp_add_refs(data,loads,{0x1200})[0x1200]==[]
print('Inventory action type scanning tests passed')
