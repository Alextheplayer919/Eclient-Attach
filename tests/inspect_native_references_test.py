import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('inspection', Path(__file__).resolve().parents[1] / 'tools/inspect-native-references.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
lines = ['00000000 FDE cie=000000 pc=000000000ccc9000..000000000ccc9300\n',
         '00000010 FDE cie=000000 pc=000000000ccc9300..000000000ccc9400\n']
assert m.unwind_ranges(lines, {'a': 0xccc9270, 'b': 0xccc9330, 'c': 0}) == {'a': (0xccc9000, 0xccc9300), 'b': (0xccc9300, 0xccc9400)}
import struct
# BL forward and backward; non-BL branches and truncated trailing bytes ignored.
data = struct.pack('<III', 0x94000002, 0x17ffffff, 0x97fffffe) + b'X'
assert m.direct_bl_calls(memoryview(data), 0x1000, {0x1008, 0x1000}) == {0x1008: [0x1000], 0x1000: [0x1008]}
print('Native reference inspection tests passed')
