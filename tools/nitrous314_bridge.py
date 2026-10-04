"""Authenticated bounded original nitrous phase and live-engine bridges.

Explicit synthetic contexts are not a complete event0 driving replay. No
import, allocator, audio/effect or unproved caller is stubbed. Retail input
stays read-only; reports require a fresh output directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from event0_trace import Image, digest_file, manifest
from nitrous314_recovery import Oracle, EXE_SHA


def phase_witnesses(image):
    machine = Oracle(image, ((0x42D470, 0x42D4F0), (0x447783, 0x4477A6)),
                     (0x42D4E1, 0x4477A6))
    u, x = machine.uc, machine.x
    view, stage = 0x1001000, 0x1003000
    machine.write(0x7361F0, stage)
    expected_table = (0, 0, 7, 7, None, None, None, 1, None, 6, None)
    rows = []
    for value in range(13):
        for ae, af, finish, racer in ((0, 0, 0, 0), (1, 0, 0, 0),
                                      (0, 1, 0, 0), (0, 0, 1, 0),
                                      (0, 0, 1, 1)):
            machine.write(stage, value)
            machine.write(view + 0x58, finish)
            machine.write(view + 0x60, 0)
            u.mem_write(view + 0x4AE, bytes((ae, af)))
            u.mem_write(0x700B3C, bytes((racer,)))
            machine.write(machine.stack, machine.stop)
            u.reg_write(x.UC_X86_REG_ESP, machine.stack)
            u.reg_write(x.UC_X86_REG_EAX, view)
            u.emu_start(0x42D470, 0x42D4E1, count=20000)
            actual = u.reg_read(x.UC_X86_REG_ECX)
            expected = expected_table[value] if value < 11 else 0
            if expected is None:
                expected = 3 if af else (4 if ae else 2)
            if finish:
                expected = 8
            if racer:
                expected = 5
            if u.reg_read(x.UC_X86_REG_EIP) != 0x42D4E1 or actual != expected:
                raise AssertionError('original stage selection mismatch')
            rows.append(dict(stage=value, flag_4ae=ae, flag_4af=af,
                             finish=finish, racer_flag=racer, selected=actual,
                             receiver='player/view object, not actor RealController'))
    # Original main-loop suffix iterates two registered view objects. Make
    # current states equal to the selected states to omit allocator-backed
    # input re-registration. The selector and actual list loop still execute.
    loop = Oracle(image, ((0x42D470, 0x42D4F0), (0x447783, 0x4477A6)),
                  (0x4477A6,))
    lu, lx = loop.uc, loop.x
    loop.write(0x7361F0, stage)
    loop.write(stage, 4)
    loop.write(0x7361C4, 2)
    for index, selected in enumerate((2, 3)):
        at = view + index * 0x1000
        loop.write(0x7361BC + index * 4, at)
        loop.write(at + 0x278, selected)
        lu.mem_write(at + 0x4AE, bytes((selected == 4, selected == 3)))
    loop.write(loop.stack, loop.stop)
    lu.reg_write(lx.UC_X86_REG_ESP, loop.stack)
    lu.reg_write(lx.UC_X86_REG_EBX, 0)
    lu.emu_start(0x447783, 0x4477A6, count=20000)
    if lu.reg_read(lx.UC_X86_REG_EIP) != 0x4477A6 or lu.reg_read(lx.UC_X86_REG_ESI) != 2:
        raise AssertionError('original registered-view list loop did not finish')
    return rows, dict(entry='447783', endpoint='4477A6', view_count=2,
                      context='current states match; re-registration omitted',
                      reached=sorted('%08X' % at for at in loop.reached))


def engine_witnesses(image):
    ranges = ((0x46C7C0, 0x46C8E0), (0x477637, 0x477684))
    machine = Oracle(image, ranges, (0x46C8E0, 0x477684))
    u, x = machine.uc, machine.x
    engine, physical, tuning, owner = 0x1004000, 0x1005000, 0x1006000, 0x1007000
    rows = []
    for idle, redline, maximum in ((800, 7500, 8000), (1000, 9000, 9500),
                                    (500, 5000, 6000)):
        u.mem_write(physical, struct.pack('<3f', idle, redline, maximum))
        machine.write(machine.stack, machine.stop)
        machine.write(machine.stack + 4, tuning)
        machine.write(0x73AD34, 10000)
        u.reg_write(x.UC_X86_REG_ESP, machine.stack)
        u.reg_write(x.UC_X86_REG_ESI, engine)
        u.reg_write(x.UC_X86_REG_ECX, physical)
        u.emu_start(0x46C7C0, 0x46C8E0, count=20000)
        descriptor = struct.unpack('<I3fI', u.mem_read(engine + 0x60, 20))
        scale = struct.unpack('<f', struct.pack('<f', 8 / (redline - idle)))[0]
        expected = (9, float(idle), float(redline), scale, 0x6FFF34)
        if u.reg_read(x.UC_X86_REG_EIP) != 0x46C8E0 or descriptor != expected:
            raise AssertionError('original engine nitrous descriptor differs')
        if struct.unpack('<2I', u.mem_read(engine + 0x84, 8)) != (0, 0):
            raise AssertionError('original engine did not initialize nitrous state')
        rows.append(dict(name='descriptor-constructor-prefix', idle=idle,
                         redline=redline, maximum=maximum, descriptor=descriptor,
                         context='engine constructor through endpoint46C8E0'))
    machine.write(owner + 0x48, engine)
    machine.write(owner + 0x58, machine.obj)
    tick_seconds = struct.unpack('<f', image.read(0x6CCA38, 4))[0]
    for name, state, prior_active, prior_seconds, clock, wanted_active in (
        ('inactive', [8000, 4000, 8000, 0], 0, 1.25, 10000, 0),
        ('new-active', [8000, 8000, 8000, 10000], 0, 1.25, 11000, 1),
        ('held-active', [8000, 8000, 8000, 10000], 1, 1.25, 12000, 1),
        ('exact-exhaustion', [8000, 8000, 8000, 10000], 1, 1.25, 18000, 1),
        ('past-exhaustion', [8000, 8000, 8000, 10000], 1, 1.25, 18001, 0),
        ('released', [8000, 0, 7000, 0], 1, 1.25, 12000, 0),
        ('empty', [8000, 0, 0, 0], 0, 1.25, 12000, 0),
        ('negative-clock-word', [8000, 8000, 8000, 0x80000000], 0, 1.25, 0x80000000, 1),
        ('inactive-sentinel', [8000, 8000, 8000, 0x7FFFFFFF], 0, 1.25, 12000, 0)):
        machine.prepare(state, clock)
        machine.write(engine + 0x84, prior_active)
        u.mem_write(engine + 0x88, struct.pack('<f', prior_seconds))
        machine.write(machine.stack + 0x18, owner)
        u.reg_write(x.UC_X86_REG_ESP, machine.stack)
        u.reg_write(x.UC_X86_REG_EBX, 0)
        u.emu_start(0x477637, 0x477684, count=20000)
        active, seconds = struct.unpack('<If', u.mem_read(engine + 0x84, 8))
        signed_clock = clock if clock < 0x80000000 else clock - 0x100000000
        expected_seconds = struct.unpack('<f', struct.pack('<f', signed_clock * tick_seconds))[0] \
            if wanted_active and not prior_active else prior_seconds
        if u.reg_read(x.UC_X86_REG_EIP) != 0x477684 or (active, seconds) != (wanted_active, expected_seconds):
            raise AssertionError('original resource-to-engine bridge differs')
        rows.append(dict(name=name, initial_resource=state, resource=machine.state(),
                         prior_active=prior_active, prior_seconds=prior_seconds,
                         clock=clock, active=active, seconds=seconds,
                         context='physics owner+58 resource and+48 engine'))
    return rows, sorted('%08X' % at for at in machine.reached)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents or out in root.parents:
        parser.error('output must be outside input root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        if digest_file(root / 'speed.exe') != EXE_SHA:
            raise ValueError('unsupported executable SHA-256')
        import unicorn
        if unicorn.__version__ != '2.1.4':
            raise ValueError('expected Unicorn2.1.4')
        image = Image((root / 'speed.exe').read_bytes())
        phases, loop = phase_witnesses(image)
        engines, reached = engine_witnesses(image)
        report = dict(executable_sha256=EXE_SHA, phases=phases, view_loop=loop,
                      engines=engines, engine_reached=reached)
        (out / 'bridge.json').write_text(json.dumps(report, indent=2) + '\n', encoding='ascii')
        print('PASS: %d original phase cases, registered-view loop, %d engine cases' %
              (len(phases), len(engines)))
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail input tree changed')
        print('PASS: retail manifests identical SHA256 ' + hashlib.sha256(before).hexdigest().upper())


if __name__ == '__main__':
    main()
