"""Authenticated original manual garage camera, including487F50 return.

Executes constructor, original slot setter and4870C0 through view selection;
only the later renderer submission is excluded. No camera calls are stubbed.
Writes the bounded native samples used by frontend_tests.cpp.
"""
from pathlib import Path
import argparse
import hashlib
import struct
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path.cwd() / 'tools'))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

parser = argparse.ArgumentParser()
parser.add_argument('--retail-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
raw = (args.retail_root / 'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image = Image(raw)
image.sections = [(a, s, r) for a, s, r, name in image.sections]
machine = Machine(image)
cam, stack, view = 0x1001000, 0x1005000, 0x1003000
machine.uc.mem_write(0x6d4ac0, bytes(machine.uc.mem_read(0x6d4be0, 0x47 * 4)))
machine.write(0x6d4d00, 0)
machine.write(0x735f0c, 0)

def write_float(at, value):
    machine.uc.mem_write(at, struct.pack('<f', value))

def update():
    machine.uc.reg_write(UC_X86_REG_ECX, cam)
    machine.uc.reg_write(UC_X86_REG_ESP, stack)
    machine.uc.emu_start(0x4870c0, 0x48752c, count=10000)
    assert machine.uc.reg_read(UC_X86_REG_EIP) == 0x48752c

def floats(at, count):
    return struct.unpack('<' + 'f' * count, machine.uc.mem_read(at, count * 4))

def literal(value):
    text = format(value, '.10g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'F'

rows = []
for slot in (0, 1, 17, 31, 32, 33, 34, 37, 39, 41):
    machine.uc.reg_write(UC_X86_REG_ESI, cam)
    machine.uc.reg_write(UC_X86_REG_ESP, stack)
    machine.uc.emu_start(0x486c93, 0x487069, count=1000)
    assert machine.uc.reg_read(UC_X86_REG_EIP) == 0x487069
    if slot == 0:
        bits = struct.unpack('<11I', struct.pack('<11f', 93, 115, 6, 0, 45, 0, .75, 0, 0, 0, .75))
    else:
        address = next(at for at in range(0x6fb0b0, 0x6fb6e0, 48) if machine.integer(at) == slot)
        bits = struct.unpack('<11I', machine.uc.mem_read(address + 4, 44))
    machine.run(0x487830, bits, {UC_X86_REG_ECX: cam})
    write_float(0x73ad38, 1 / 60)
    for _ in range(600):
        update()
    machine.write(cam + 0x2c, 3)
    write_float(cam + 0x180, 0)
    for frame in range(420):
        # Same finite stick deltas as the port caller. First sweep reaches
        # the lower pitch bound, second reaches the upper, then release.
        yaw = 3 if frame < 300 else 0
        pitch = -1 if frame < 180 else 1 if frame < 300 else 0
        write_float(cam + 0x184, pitch)
        write_float(cam + 0x188, yaw)
        update()
        if frame not in (0, 1, 30, 119, 179, 180, 239, 299, 300, 359, 419):
            continue
        state = floats(cam + 0x40, 5) + floats(cam + 0x60, 3) + floats(cam + 0x70, 3)
        selected = machine.uc.reg_read(UC_X86_REG_EAX)
        machine.run(0x487680, (view,), {UC_X86_REG_EAX: selected})
        rows.append((slot, frame, state, floats(view, 16)))

lines = ['// tools/frontend_orbit_native.py; original PC1.4 USA camera, no camera stubs.',
         'struct ManualCameraSample { unsigned slot,frame; float state[11],view[16]; };',
         'const ManualCameraSample manual_camera_samples[]={']
for slot, frame, state, matrix in rows:
    lines.append('    {' + str(slot) + ',' + str(frame) + ',{' + ','.join(map(literal, state)) + '},{' + ','.join(map(literal, matrix)) + '}},')
lines.append('};')
args.output.write_text('\n'.join(lines) + '\n', encoding='ascii')
print('PASS original manual orbit samples:', len(rows), 'across10 slots; pitch85..140, distance6, synchronized return')
