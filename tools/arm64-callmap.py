#!/usr/bin/env python3
"""Read-only AArch64 ELF direct-call mapper; addresses are ELF virtual addresses."""
import argparse
import bisect
import hashlib
import json
import mmap
import re
import struct
import subprocess
from pathlib import Path

PINNED = '65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1'
DEFAULTS = {'terrain-atlas-load': 0x65f36b8, 'texture-item-diagnostic': 0x66e5a38,
            'block-tessellator-constructor-name': 0x675fcb4, 'block-tessellator-color-name': 0x675fee8,
            'block-pipeline-name': 0x6765828, 'terrain-json-second': 0x9fcaab8}
FDE = re.compile(r'\bFDE\b.*?\bpc=([0-9a-fA-F]+)\.\.([0-9a-fA-F]+)')


def branch(word, pc):
    # B/BL immediate; B is a possible tail-call or intra-function jump, not necessarily a call.
    op = word & 0xfc000000
    if op not in (0x14000000, 0x94000000):
        return None
    imm = word & 0x03ffffff
    if imm & 0x02000000:
        imm -= 0x04000000
    return ('bl' if op == 0x94000000 else 'b', pc + (imm << 2))


def parse_fdes(lines):
    ranges = set()
    for line in lines:
        m = FDE.search(line)
        if m:
            start, end = (int(x, 16) for x in m.groups())
            if start < end:
                ranges.add((start, end))
    return sorted(ranges)


class RangeIndex:
    def __init__(self, ranges):
        self.ranges = sorted(ranges)
        self.starts = [r[0] for r in self.ranges]
        self.max_ends = []
        highest = 0
        for _, end in self.ranges:
            highest = max(highest, end)
            self.max_ends.append(highest)

    def find(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        matches = []
        while i >= 0 and self.max_ends[i] > addr:
            start, end = self.ranges[i]
            if start <= addr < end:
                matches.append((start, end))
            i -= 1
        # Overlapping FDEs are ambiguous; do not silently attribute them.
        return matches[0] if len(matches) == 1 else None


def segments(data):
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('expected little-endian ELF64 AArch64')
    off = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size < 56 or not count or count > 1024 or off + size * count > len(data):
        raise ValueError('invalid program headers')
    result = []
    for i in range(count):
        typ, flags, fileoff, va, _, filesz, _, _ = struct.unpack_from('<IIQQQQQQ', data, off+i*size)
        if typ == 1 and flags & 1:
            if fileoff + filesz > len(data) or va % 4 or fileoff % 4:
                raise ValueError('invalid executable segment')
            result.append((fileoff, va, filesz))
    if not result:
        raise ValueError('no executable load segments')
    return result


def analyze(data, fdes, sites, limit=100):
    index = RangeIndex(fdes)
    entries = {}
    for name, addr in sites.items():
        r = index.find(addr)
        entries[name] = {'site': hex(addr), 'fde': [hex(x) for x in r] if r else None,
                         'incoming': [], 'outgoing': [], 'incoming_total': 0, 'outgoing_total': 0}
    by_range = {}
    for name, entry in entries.items():
        if entry['fde']:
            r = tuple(int(x, 16) for x in entry['fde'])
            by_range.setdefault(r, []).append(name)
    for fileoff, va, size in segments(data):
        # Decode raw immediate B/BL, including possible literal data in executable segments.
        for pos in range(fileoff, fileoff + (size & ~3), 4):
            word = struct.unpack_from('<I', data, pos)[0]
            if word & 0x7c000000 != 0x14000000:
                continue
            pc = va + pos - fileoff
            kind, target = branch(word, pc)
            src, dst = index.find(pc), index.find(target)
            if src == dst:
                continue
            edge = {'instruction': hex(pc), 'kind': kind, 'target': hex(target),
                    'source_fde': [hex(x) for x in src] if src else None,
                    'target_fde': [hex(x) for x in dst] if dst else None,
                    'target_is_fde_start': bool(dst and target == dst[0])}
            for r, direction in ((src, 'outgoing'), (dst, 'incoming')):
                for name in by_range.get(r, ()):
                    entry = entries[name]
                    entry[direction+'_total'] += 1
                    if len(entry[direction]) < limit:
                        entry[direction].append(edge)
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lib', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--site', action='append', default=[], help='NAME=ELF_VADDR (repeatable)')
    parser.add_argument('--limit', type=int, default=100, help='maximum listed edges per direction and site')
    args = parser.parse_args()
    if not 0 <= args.limit <= 10000:
        parser.error('limit must be between 0 and 10000')
    with args.lib.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    sites = {}
    for site in args.site:
        try:
            name, value = site.split('=', 1)
            addr = int(value, 0)
            if not name or name in sites or addr < 0:
                raise ValueError()
            sites[name] = addr
        except ValueError:
            parser.error('sites must have unique names and nonnegative addresses: NAME=0xADDRESS')
    if not sites:
        if digest != PINNED:
            parser.error('default sites apply only to the pinned library; pass --site explicitly')
        sites = DEFAULTS
    proc = subprocess.Popen(['readelf', '--debug-dump=frames', str(args.lib)], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True)
    fdes = parse_fdes(proc.stdout)
    proc.stdout.close()
    if proc.wait() or not fdes:
        raise RuntimeError('readelf could not decode FDEs')
    with args.lib.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        report = analyze(data, fdes, sites, args.limit)
    result = {'sha256': digest, 'fde_count': len(fdes), 'sites': report,
              'limitations': ['Static candidates only; not verified function names, runtime addresses or callable ABIs.',
                              'FDEs can be missing, overlapping, or represent partial functions.',
                              'Only direct B/BL are decoded; indirect and virtual calls are omitted.',
                              'B may be a local branch, not a tail call; executable sections can contain data.',
                              'Lists are capped; totals include all matching direct branches.']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    for name, entry in report.items():
        print(f'{name}: FDE={entry["fde"]}, incoming={entry["incoming_total"]}, outgoing={entry["outgoing_total"]}')


if __name__ == '__main__':
    main()
