"""PC1.4 package lookup, purchase brands and turbo identity witnesses.

Original x86 executes against authenticated read-only retail data. UI rendering
and devices are outside the boundary; there are no external call hooks.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from world_body_trace import Image, jdlz, chunks
from physics256_native import Machine

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--out', type=Path)
parser.add_argument('--write-fixture', action='store_true')
args = parser.parse_args()
root = Path.cwd()
if args.out:
    relative = args.out.resolve().relative_to(root)
    assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
    assert args.root.resolve() not in args.out.resolve().parents
    args.out.mkdir(parents=True, exist_ok=False)
raw = (args.root / 'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
front = jdlz((args.root / 'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest() == '94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
machine = Machine(Image(raw))
machine.allowed = [(0x5a0220, 0x5a026c), (0x5a0270, 0x5a0290),
                   (0x5a02c0, 0x5a02ed), (0x50bca2, 0x50bd48), (0x50aac0, 0x50ab00)]
source = 0x1010008  # Preserve the chunk header's original alignment.
group_at, _ = next((at, end) for tag, at, end in chunks(front) if tag == 0x34a09)
package_at, package_end = next((at, end) for tag, at, end in chunks(front) if tag == 0x34a0a)
machine.uc.mem_write(source, front[group_at:package_end])
machine.run(0x5a0270, (source,))
assert machine.reg('eax') == 1
groups = [list(struct.unpack('<4I', machine.uc.mem_read(machine.get(0x734400) + 16 * field, 16)))
          for field in range(9)]
machine.run(0x5a02c0, (source + package_at - group_at,))
assert machine.reg('eax') == 1
rows, grants = [], []
node, ride = 0x1100000, 0x1110000
for field, *counts in groups:
    for level, count in enumerate(counts, 1):
        for number in range(1, count + 1):
            machine.run(0x5a0220, (field,), esi=number, edi=level)
            record = machine.reg('eax')
            assert record
            data = bytes(machine.uc.mem_read(record, 16))
            original_at = record - source + group_at
            assert data == front[original_at:original_at + 16], 'mapped package must equal complete retail record'
            assert struct.unpack_from('<3H', data) == (field, level, number)
            brands = list(data[7:7 + data[6]])
            assert brands and len(brands) <= 9 and all(b < 68 for b in brands)
            rows.append([field, level, number, len(brands)] + brands + [0] * (9 - len(brands)))
            machine.put(node + 12, record)
            machine.put(0x744ff8, ride)
            machine.put(0x6f889c, field)
            machine.put(ride + 12, 255, 'B')
            for brand in range(68):
                machine.put(0x760158 + 4 * brand, 4)
            machine.run(0x50bca2, end=0x50bd48, esi=node)
            unlocked = [brand for brand in range(68) if machine.get(0x760158 + brand * 4) != 4]
            assert unlocked == sorted(set(brands))
            assert all(machine.get(0x760158 + brand * 4) == 7 for brand in unlocked)
            turbo = machine.get(ride + 12, 'b')
            expected = {6: 0, 20: 1, 22: 2, 46: 3, 50: 4}.get(brands[-1], -1) if field == 3 else -1
            assert turbo == expected
            grants.append([field, level, number, turbo])
        machine.run(0x5a0220, (field,), esi=count + 1, edi=level)
        assert machine.reg('eax') == 0
assert len(rows) == machine.get(0x734408) and not machine.events
progression = []
machine.put(0x7677ac, 0)
machine.put(0x735ec1, 0, 'B')
machine.put(node + 0x90, 0)
for current in range(4):
    machine.put(0x745728, current)
    for target in range(1, 4):
        machine.run(0x50aac0, (node, 0, target))
        permitted = bool(machine.reg('eax') & 255)
        assert permitted == (current + 1 == target)
        progression.append([current, target, int(permitted)])
# Original4AB120/4AB200 copy the complete Quick Ride and Underground
# customization regions, including their level/turbo fields and sponsor flags.
# Use synthetic bytes, never a live profile or an installed PC save.
codec = Machine(Image(raw))
codec.allowed = [(0x4ab120, 0x4ab1f8), (0x4ab200, 0x4ab2aa)]
profile, restored, saved = 0x7588c4, 0x1120000, 0x10a0000
sample = bytes((i * 29 + 7) & 255 for i in range(0xf944))
codec.uc.mem_write(profile, sample)
codec.run(0x4ab120, (profile, 0xf660), eax=saved)
codec.run(0x4ab200, eax=saved, ecx=0xf660, edx=restored)
codec_ranges = [(0x210, 4), (0x214, 0x80), (0x364, 0x62d0),
                (0x6634, 0x88b4), (0xeee8, 4), (0xeef4, 0xa50)]
for offset, size in codec_ranges:
    assert bytes(codec.uc.mem_read(restored + offset, size)) == sample[offset:offset + size]
assert not codec.events
fixture = '// Original5A0270/5A02C0/5A0220 and50BCA2..50BD48; no call hooks.\n'
fixture += 'static constexpr unsigned kNativePerformanceGroups[][4]={\n'
fixture += ''.join('    {' + ','.join(str(v) + 'U' for v in row) + '},\n' for row in groups) + '};\n'
fixture += 'static constexpr unsigned kNativePerformancePackages[][13]={\n'
fixture += ''.join('    {' + ','.join(str(v) + 'U' for v in row) + '},\n' for row in rows) + '};\n'
fixture += 'static constexpr int kNativePerformanceTurbo[][4]={\n'
fixture += ''.join('    {' + ','.join(map(str, row)) + '},\n' for row in grants) + '};\n'
fixture += 'static constexpr unsigned kNativePerformanceProgression[][3]={\n'
fixture += ''.join('    {' + ','.join(str(v) + 'U' for v in row) + '},\n' for row in progression) + '};\n'
target = root / 'tests/performance_packages_native.inc'
if args.write_fixture:
    target.write_text(fixture, encoding='ascii')
else:
    assert target.read_text(encoding='ascii') == fixture, 'package witness fixture is stale'
if args.out:
    (args.out / 'evidence.json').write_text(json.dumps(dict(scope=__doc__, groups=groups,
        packages=rows, grants=grants, progression=progression, hooks=machine.events,
        codec_ranges=codec_ranges, codec_hooks=codec.events), ensure_ascii=True, indent=2) + '\n', encoding='ascii')
print('PASS original performance groups, lookup/order and purchase brands:', len(rows), 'packages; no call hooks')
print('PASS original turbo brand identities:', len(grants), 'purchase slices; missing variant rejected')
print('PASS original PC save codec round trip: complete Quick Ride/career customization and sponsor regions; synthetic data, no hooks')
