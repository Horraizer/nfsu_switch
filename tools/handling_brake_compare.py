"""Issue251 authenticated isolated brake latch and completed RPM comparison.

No retail process is launched. The strut pass is an explicit stub: input
wheel spins, setup, gear and prior flags are synthetic. This is not live
driving evidence, a stopping-distance reference or complete coupling parity.
Run from the assigned tree with standalone Python, Capstone and Unicorn.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

from world_body_trace import Image, manifest
from physics256_native import Machine

EXE_SHA = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
BUN_SHA = '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def neighbour(value, delta):
    word = struct.unpack('<I', struct.pack('<f', value))[0]
    return struct.unpack('<f', struct.pack('<I', word + delta))[0]


def compare(root, probe, out):
    data = (root / 'speed.exe').read_bytes()
    bun = (root / 'Global/GLOBALB.BUN').read_bytes()
    if hashlib.sha256(data).hexdigest() != EXE_SHA or hashlib.sha256(bun).hexdigest() != BUN_SHA:
        raise ValueError('unsupported retail inputs')
    record = bun[1736752:1736752 + 3216]
    if hashlib.sha256(record).hexdigest() != '212f69ad8e739250f95e04c54fb6613a39a7c76014cb402df966f64b5d9fff9b':
        raise ValueError('unsupported stock240SX record')
    image = Image(data)
    machine = Machine(image)
    machine.allowed = [(0x4757D0, 0x4759FF), (0x475530, 0x4757C8)]
    machine.hook(0x475A00, 'strut-pass-not-executed-synthetic-wheel-input', pop=4)
    engine, setup, derivative = 0x1100000, 0x1101000, 0x1102000
    # The engine receives the setup subobject at retail record+0x110.
    machine.uc.mem_write(setup, record[0x110:])
    machine.put(engine + 0x18, setup)
    rows = []
    axes = [0.0, neighbour(0.2, -1), f32(0.2), neighbour(0.2, 1),
            0.5, neighbour(0.95, -1), f32(0.95), neighbour(0.95, 1), 1.0]
    speeds = [-30.0001, -30.0, -29.9999, 0.0, 29.9999, 30.0, 30.0001]
    for gear in (0, 2, 3):
        for axis in axes:
            for speed in speeds:
                for prior in (0, 1):
                    spin = f32(speed)
                    machine.put(engine + 0x124, gear)
                    machine.put(engine + 0x134, gear)
                    machine.put(engine + 0x12C, axis, 'f')
                    for k in range(4):
                        machine.put(engine + 0x24 + 4*k, spin, 'f')
                        machine.put(engine + 0xB8 + 4*k, prior)
                    machine.run(0x4757D0, ecx=engine)
                    rows.append(dict(axis=axis, before_spin=spin, before_locked=prior,
                        gear=gear, after_spin=machine.get(engine + 0x24, 'f'),
                        after_locked=machine.get(engine + 0xB8),
                        original_rpm=machine.get(engine + 0x120, 'f')))
    # Consumer witness: locked wheels bypass torque curves, feedback and
    # brake damping, producing exactly zero angular derivative.
    for k in range(4):
        machine.put(engine + 0xB8 + 4*k, 1)
        machine.put(derivative + 4*k, 123.0, 'f')
    machine.run(0x475530, (derivative,), ecx=engine)
    if any(machine.get(derivative + 4*k, 'f') != 0.0 for k in range(4)):
        raise RuntimeError('original locked-wheel derivative was nonzero')
    inputs = ''.join('%.17g %.17g %d\n' % (r['axis'], r['before_spin'], r['before_locked']) for r in rows)
    run = subprocess.run([str(probe)], input=inputs.encode('ascii'), stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, check=False)
    (out / 'port.log').write_bytes(run.stdout)
    (out / 'port.stderr.log').write_bytes(run.stderr)
    if run.returncode or run.stderr or b'error' in run.stdout.lower():
        raise RuntimeError('port comparison bridge failed')
    lines = run.stdout.decode('ascii').splitlines()
    if len(lines) != len(rows):
        raise RuntimeError('port comparison bridge row count differs')
    for row, line in zip(rows, lines):
        spin, locked = line.split()
        if not math.isfinite(float(spin)) or float(spin) != row['after_spin'] or int(locked) != row['after_locked']:
            raise RuntimeError('port differs: ' + json.dumps(row))
    report = dict(executable_sha256=EXE_SHA, globalb_sha256=BUN_SHA,
        source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode('ascii').strip(),
        source_hashes={p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in
                       ('src/vehicle.cpp', 'include/nfsu/vehicle.h', 'tools/handling_brake_compare.cpp')},
        probe_sha256=hashlib.sha256(probe.read_bytes()).hexdigest(),
        boundary='Original completed engine/RPM with synthetic wheel/setup/gear/flags; strut pass explicitly stubbed; no live game',
        x87_control_word='0x007F synthetic, live environment unverified',
        allowed=[['%08X' % a, '%08X' % b] for a, b in machine.allowed],
        visited=['%08X' % a for a in sorted(machine.visited)],
        external_hooks=machine.events, locked_derivative=[0.0]*4, rows=rows)
    (out / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=True)+'\n', encoding='ascii')
    print('PASS: original/port brake latch cases %d; original locked-wheel derivative zero' % len(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'probe', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    relative = out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root == out or root in out.parents:
        parser.error('output must be a fresh ASCII build directory in the assigned workspace, outside retail')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out/'before.json').write_bytes(before)
    try:
        compare(root, args.probe.resolve(), out)
    finally:
        after = manifest(root)
        (out/'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__ == '__main__':
    main()
