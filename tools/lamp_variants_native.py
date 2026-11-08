"""Compare all fitted lamp emitters with original56CC40, allocation stub only.

The probe exercises real part selection, assembly and sprite preparation.
This witness executes the original collector against each selected solid's
unaltered marker records. Car translation is zero and the car type is present.
It does not claim damage-state, occlusion or full native rendering parity.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

parser = argparse.ArgumentParser()
parser.add_argument('--retail-root', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
args = parser.parse_args()
raw = (args.retail_root / 'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image = Image(raw)
image.sections = [(a, s, r) for a, s, r, _ in image.sections]
run = subprocess.run([str(args.probe.resolve()), str(args.retail_root)],
                     capture_output=True, text=True, check=True)
assert not run.stderr and 'PASS lamp variants 200 ' in run.stdout
parts = []
for line in run.stdout.splitlines():
    fields = line.split()
    if fields[0] == 'PART':
        parts.append({'identity': fields[1:], 'sources': [], 'anchors': []})
    elif fields[0] == 'SOURCE':
        parts[-1]['sources'].append((int(fields[1]), [float(x) for x in fields[2:]]))
    elif fields[0] == 'ANCHOR':
        parts[-1]['anchors'].append((int(fields[1]), [float(x) for x in fields[2:]]))
assert len(parts) == 200
hashes = [0x9DB90133, 0xD09091C6, 0x31A66786, 0xBF700A79, 0xA2A2FC7C]
total = 0
for part in parts:
    machine = Machine(image)
    owner, solid, markers, heap = 0x1000000, 0x1005000, 0x1006000, 0x1010000
    machine.write(owner + 8, 1)
    slot = int(part['identity'][1])
    machine.write(owner + 0xE78 + slot * 0x60, solid)
    machine.write(solid + 0x80, markers)
    machine.uc.mem_write(solid + 0x1B, bytes((len(part['sources']),)))
    sentinel = owner + 0x500
    machine.write(sentinel, sentinel)
    machine.write(sentinel + 4, sentinel)
    for index, (group, matrix) in enumerate(part['sources']):
        record = struct.pack('<4I16f', hashes[group], 0, 0, 0, *matrix)
        machine.uc.mem_write(markers + index * 80, record)
    allocated = []

    def allocate(uc, at, size, _):
        if at != 0x67121A:
            return
        sp = uc.reg_read(UC_X86_REG_ESP)
        assert machine.integer(sp + 4) == 0x50
        block = heap + len(allocated) * 0x80
        allocated.append(block)
        uc.reg_write(UC_X86_REG_EAX, block)
        uc.reg_write(UC_X86_REG_EIP, machine.integer(sp))
        uc.reg_write(UC_X86_REG_ESP, sp + 4)

    hook = machine.uc.hook_add(UC_HOOK_CODE, allocate)
    machine.run(0x56CC40, (owner,))
    machine.uc.hook_del(hook)
    original = []
    node = machine.integer(sentinel)
    while node != sentinel:
        assert node in allocated and len(original) < len(allocated)
        group = hashes.index(machine.integer(node + 8))
        position = struct.unpack('<3f', machine.uc.mem_read(node + 0x10, 12))
        normal = struct.unpack('<3f', machine.uc.mem_read(node + 0x20, 12))
        original.append((group, struct.pack('<6f', *position, *normal)))
        node = machine.integer(node)
    actual = [(g, struct.pack('<6f', *values)) for g, values in part['anchors']]
    assert collections.Counter(original) == collections.Counter(actual), part['identity']
    assert len(original) == len(part['sources'])
    total += len(original)
print(f'PASS: 200 fitted front/rear variants, {total} emitters match genuine original56CC40 positions/normals/counts')
print('BOUNDARY: allocator supplied; zero car translation; selected LOD-A solids; damage and full rendered pixels are separate checks')
