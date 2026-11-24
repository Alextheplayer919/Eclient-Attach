import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/check-internalcpp-vtables.py'
s=importlib.util.spec_from_file_location('vtablecheck',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
c={'Test':{'vtable':'0x1000','slots':[{'slot':0,'address':'0x2000'},{'slot':1,'address':'0x3000'},{'slot':2,'address':'0x4000'}]}}
t=m.targets(c)
assert set(t)=={0x1010,0x1018,0x1020}
lines=['0000000000001010  0000000000000403 R_AARCH64_RELATIVE 2000',
       '0000000000001018  0000000000000403 R_AARCH64_RELATIVE 3004',
       '0000000000001020  0000000000000101 R_AARCH64_ABS64 0 foo + 0']
r=m.compare(c,m.read_relocations(lines,t))['Test']
assert (r['match'],r['different'],r['unresolved'])==(1,1,1)
print('Internal C++ vtable cross-check tests passed')
