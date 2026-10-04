"""Issue 314 authenticated, bounded nitrous resource witnesses.

Synthetic contexts execute original PE instructions, not a retail driving replay.
No OS imports or game process run. Output is fresh and inputs stay read-only.
Python 3.8+ with Unicorn 2.1.4. This does not implement event0 nitrous.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from event0_trace import Image, digest_file, manifest

EXE_SHA = 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'
SENTINEL = 0x7FFFFFFF


class Oracle:
    def __init__(self, image):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as x
        self.x = x
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0x400000, 0x400000)
        self.uc.mem_map(0x1000000, 0x100000)
        for start, size, raw, _ in image.sections:
            self.uc.mem_write(start, image.data[raw:raw + size])
        self.obj, self.stack, self.stop = 0x1000000, 0x10F0000, 0x10FF000
        self.reached = set()
        allowed = ((0x464DC7, 0x464E6A), (0x465430, 0x465487), (0x465490, 0x4654E9),
                   (0x4654F0, 0x46552B), (0x465530, 0x4655AD),
                   (0x674898, 0x67490D))

        def hook(cpu, address, size, _):
            if address == 0x464E6A:
                cpu.emu_stop()
                return
            if address == self.stop:
                return
            if not any(a <= address and address + size <= b for a, b in allowed):
                raise RuntimeError('execution outside allowlist: %08X' % address)
            self.reached.add(address)
        self.uc.hook_add(UC_HOOK_CODE, hook)

    def write(self, at, value):
        self.uc.mem_write(at, struct.pack('<I', value & 0xFFFFFFFF))

    def state(self):
        return list(struct.unpack('<4I', self.uc.mem_read(self.obj + 0x3B0, 16)))

    def prepare(self, state, clock, mode=1):
        for offset, value in enumerate(state):
            self.write(self.obj + 0x3B0 + 4 * offset, value)
        self.write(0x73AD34, clock)
        self.write(0x7000E8, mode)

    def call(self, address, register):
        self.write(self.stack, self.stop)
        self.uc.reg_write(self.x.UC_X86_REG_ESP, self.stack)
        self.uc.reg_write(register, self.obj)
        self.uc.emu_start(address, self.stop, count=20000)
        if self.uc.reg_read(self.x.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError('instruction limit without return')
        return self.uc.reg_read(self.x.UC_X86_REG_EAX) & 0xFF

    def initialize(self, state, capacity, count):
        self.prepare(state, 10000)
        record = self.obj + 0x1000
        self.write(self.obj, record)
        self.uc.mem_write(record + 0x2A0, struct.pack('<f', capacity))
        self.write(record + 0x2A4, count)
        self.uc.reg_write(self.x.UC_X86_REG_ESI, self.obj)
        self.uc.reg_write(self.x.UC_X86_REG_EBX, 0)
        self.uc.reg_write(self.x.UC_X86_REG_ESP, self.stack)
        self.uc.emu_start(0x464DC7, 0x464E6A, count=20000)
        if self.uc.reg_read(self.x.UC_X86_REG_EIP) != 0x464E6A:
            raise RuntimeError('initializer instruction limit')
        return self.state()


def witnesses(image):
    oracle = Oracle(image)
    x = oracle.x
    rows = []

    def check(name, state, clock, address, register, expected, result=None, mode=1):
        oracle.prepare(state, clock, mode)
        actual_result = oracle.call(address, register)
        actual = oracle.state()
        if actual != expected or (result is not None and actual_result != result):
            raise AssertionError('%s: native state=%r result=%r' % (name, actual, actual_result))
        rows.append(dict(name=name, input=state, clock=clock, mode=mode,
                         entry='%08X' % address, state=actual, result=actual_result))

    for capacity, count in ((2, 2), (4, 4), (6, 6), (0, 0)):
        expected = [capacity * 4000, (capacity * 4000 // count) if count else 0, capacity * 4000, 0]
        actual = oracle.initialize([0, 0, 0, 0], capacity, count)
        if actual != expected:
            raise AssertionError('native initialization mismatch: %r' % actual)
        rows.append(dict(name='initialize-capacity-%d' % capacity, input_capacity=capacity, input_count=count, state=actual))
    state = [8000, 0, 0, 0]
    if oracle.initialize(state, 2, 2) != state:
        raise AssertionError('existing initialized capacity was unexpectedly refilled')
    rows.append(dict(name='initializer-preserves-depleted-state', input=state, state=state))

    available = 0x465490
    active = 0x465430
    start = 0x4654F0
    stop = 0x465530
    for remaining in (0, SENTINEL, 0xFFFFFFFF):
        s = [2000, 1000, remaining, 0]
        check('unavailable-%08X' % remaining, s, 10000, available, x.UC_X86_REG_ECX, s, 0)
    for timestamp in (0, SENTINEL):
        s = [2000, 1000, 2000, timestamp]
        check('idle-%08X' % timestamp, s, 10000, active, x.UC_X86_REG_EAX, s, 0)
        check('available-%08X' % timestamp, s, 10000, available, x.UC_X86_REG_ECX, s, 1)
    check('held-start', [2000, 1000, 2000, 0], 10000, start, x.UC_X86_REG_EDX,
          [2000, 2000, 2000, 10000], 1)
    check('held-duplicate-start', [2000, 2000, 2000, 10000], 10250, start,
          x.UC_X86_REG_EDX, [2000, 2000, 2000, 10000], 0)
    check('held-before-empty', [2000, 2000, 2000, 10000], 11999, active,
          x.UC_X86_REG_EAX, [2000, 2000, 2000, 10000], 1)
    check('held-exact-empty', [2000, 2000, 2000, 10000], 12000, active,
          x.UC_X86_REG_EAX, [2000, 2000, 2000, 10000], 1)
    check('held-past-empty', [2000, 2000, 2000, 10000], 12250, active,
          x.UC_X86_REG_EAX, [2000, 0, 0, 0], 0)
    check('release-250-ticks', [2000, 2000, 2000, 10000], 10250, stop,
          x.UC_X86_REG_ESI, [2000, 0, 1750, 0])
    check('same-clock-stop', [2000, 2000, 2000, 10000], 10000, stop,
          x.UC_X86_REG_ESI, [2000, 0, 2000, 0])
    check('resumed-start', [2000, 0, 1750, 0], 15000, start,
          x.UC_X86_REG_EDX, [2000, 1750, 1750, 15000], 1)
    check('alternative-mode-start', [2000, 1000, 2000, 0], 10000, start,
          x.UC_X86_REG_EDX, [2000, 1000, 1000, 10000], 1, mode=0)
    check('clock-zero-sentinel', [2000, 1000, 2000, 0], 0, start,
          x.UC_X86_REG_EDX, [2000, 2000, 2000, 0], 1)
    return rows, sorted('%08X' % at for at in oracle.reached)




def torque_witnesses(image):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn import x86_const as x
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, 0x400000)
    u.mem_map(0x1000000, 0x100000)
    for a, size, raw, _ in image.sections:
        u.mem_write(a, image.data[raw:raw + size])
    engine, record, tuning, stack = 0x1000000, 0x1001000, 0x1002000, 0x10F0000
    reached = set()
    allowed = ((0x46D057, 0x46D0C4), (0x583EA0, 0x583F18), (0x674898, 0x67490D), (0x4387E0, 0x4387F6))
    def hook(cpu, at, size, _):
        if at == 0x46D0C4:
            cpu.emu_stop()
            return
        if not any(a <= at and at + size <= b for a, b in allowed):
            raise RuntimeError('torque execution outside allowlist: %08X' % at)
        reached.add(at)
    u.hook_add(UC_HOOK_CODE, hook)
    def integer(at, value):
        u.mem_write(at, struct.pack('<I', value))
    def floating(at, value):
        u.mem_write(at, struct.pack('<f', value))
    def read_float(at):
        return struct.unpack('<f', u.mem_read(at, 4))[0]
    integer(engine + 0x1C, record)
    integer(record, tuning)
    floating(tuning + 0x2AC, 0.5)
    integer(engine + 0x60, 9)
    floating(engine + 0x64, 1000)
    floating(engine + 0x68, 9000)
    floating(engine + 0x6C, 0.001)
    integer(engine + 0x70, 0x6FFF34)
    floating(engine + 0x88, 10)
    conversion = struct.unpack('<f', image.read(0x6CCB78, 4))[0]
    delay = struct.unpack('<f', image.read(0x70002C, 4))[0]
    multiplier = struct.unpack('<f', image.read(0x6B7238, 4))[0]
    curve = struct.unpack('<9f', image.read(0x6FFF34, 36))
    rows = []
    for rpm in (1000, 3000, 5000, 9000):
        for flag, override in ((0, 0), (1, 0), (0, 1)):
            for clock in (40000, 80000):
                u.reg_write(x.UC_X86_REG_ESP, stack)
                u.reg_write(x.UC_X86_REG_EDI, engine)
                floating(stack + 4, 2)
                floating(engine + 0x14, rpm / conversion)
                u.mem_write(0x78A344, bytes([flag]))
                integer(0x7361A8, override)
                integer(0x73AD34, clock)
                u.emu_start(0x46D059, 0x46D0C4, count=20000)
                if u.reg_read(x.UC_X86_REG_EIP) != 0x46D0C4:
                    raise RuntimeError('torque instruction limit without endpoint')
                actual = read_float(stack + 4)
                point = (rpm - 1000) // 1000
                expected = 2 + curve[point] * 0.5
                if (flag or override) and clock * struct.unpack('<f', image.read(0x6CCA38, 4))[0] <= 10 + delay:
                    expected *= multiplier
                if abs(actual - expected) > 2e-6:
                    raise AssertionError('live additive term mismatch: %r versus %r' % (actual, expected))
                rows.append(dict(rpm=rpm, base_torque=2, flag=flag, override=override, clock=clock,
                                 observed_torque=actual, expected_torque=expected))
    # The original branch at 46D057 reads ZF established by engine +84.
    for rpm in (1000, 5000, 9000):
        u.reg_write(x.UC_X86_REG_ESP, stack)
        u.reg_write(x.UC_X86_REG_EDI, engine)
        u.reg_write(x.UC_X86_REG_EFLAGS, 0x42)
        floating(stack + 4, 2)
        floating(engine + 0x14, rpm / conversion)
        u.emu_start(0x46D057, 0x46D0C4, count=20000)
        if u.reg_read(x.UC_X86_REG_EIP) != 0x46D0C4 or read_float(stack + 4) != 2:
            raise AssertionError('inactive nitrous changed base torque')
        rows.append(dict(rpm=rpm, base_torque=2, active=False, observed_torque=2))
    return rows, sorted('%08X' % at for at in reached)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents or out in root.parents:
        parser.error('output must be outside input root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        exe = root / 'speed.exe'
        if digest_file(exe) != EXE_SHA:
            raise ValueError('unsupported executable SHA-256')
        image = Image(exe.read_bytes())
        import unicorn
        if unicorn.__version__ != '2.1.4':
            raise ValueError('expected Unicorn 2.1.4')
        if struct.unpack('<I', image.read(0x7000E8, 4))[0] != 1:
            raise ValueError('retail initial resource mode changed')
        rows, reached = witnesses(image)
        torque_rows, torque_reached = torque_witnesses(image)
        report = dict(executable_sha256=EXE_SHA, unicorn=unicorn.__version__,
                      context='synthetic bounded PE execution; not whole-game parity',
                      witnesses=rows, reached=reached, torque_witnesses=torque_rows, torque_reached=torque_reached)
        (out / 'witnesses.json').write_text(json.dumps(report, indent=2) + '\n', encoding='ascii')
        print('PASS: %d authenticated resource witnesses' % len(rows))
        print('PASS: %d live-engine additive-term witnesses' % len(torque_rows))
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if after != before:
            raise RuntimeError('retail input tree changed')
        print('PASS: retail manifests identical SHA256 ' + hashlib.sha256(before).hexdigest().upper())


if __name__ == '__main__':
    main()