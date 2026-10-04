"""Issue 275: isolated native low-support torque contribution, not a drive replay.
Requires Unicorn 2 and standalone Python 3.8+. Uses a fresh output directory.
The native alignment branch is deliberately outside this measured scope.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess
from handling_measure import manifest
EXE_SHA = 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'

def compare(root, out, probe):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn import x86_const as x
    raw = (root/'speed.exe').read_bytes()
    if hashlib.sha256(raw).hexdigest().upper() != EXE_SHA:
        raise ValueError('unsupported speed.exe')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('not PE')
    count = struct.unpack_from('<H', raw, pe+6)[0]
    opts = struct.unpack_from('<H', raw, pe+20)[0]
    base = struct.unpack_from('<I', raw, pe+52)[0]
    size = struct.unpack_from('<I', raw, pe+80)[0]
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size+4095)&~4095)
    for k in range(count):
        at = pe+24+opts+40*k
        _, va, length, offset = struct.unpack_from('<IIII', raw, at+8)
        if length:
            u.mem_write(base+va, raw[offset:offset+length])
    heap, stack = 0x10000000, 0x20000000
    u.mem_map(heap, 0x10000)
    u.mem_map(stack, 0x10000)
    physics, car, body = heap+0x1000, heap+0x2000, heap+0x4000
    def wi(at, value): u.mem_write(at, struct.pack('<I', value))
    def wf(at, value): u.mem_write(at, struct.pack('<f', value))
    def rf(at): return struct.unpack('<f', u.mem_read(at, 4))[0]
    allowed = [(0x474dd0, 0x474e8b), (0x583f20, 0x583fe7)]
    reached = set()
    def hook(cpu, address, length, _):
        if address in (0x474e8b, 0x47505e):
            reached.add(address)
            cpu.emu_stop()
            return
        if not any(a <= address and address+length <= b for a, b in allowed):
            raise RuntimeError('execution outside allowlist: %08X' % address)
        reached.add(address)
    u.hook_add(UC_HOOK_CODE, hook)
    ptr, n = struct.unpack('<II', u.mem_read(0x705e98, 8))
    if ptr != 0x705e80 or n != 3:
        raise ValueError('unexpected damping descriptor')
    table = bytes(u.mem_read(ptr, n*8))
    built = subprocess.run([str(probe), '--table'], capture_output=True, text=True, check=True)
    if built.stderr or bytes.fromhex(built.stdout.strip()) != table:
        raise ValueError('table mismatch')
    # Binary32 contexts match the actual native storage, including neighbors.
    def f32(v): return struct.unpack('<f', struct.pack('<f', v))[0]
    first = struct.unpack('<I', struct.pack('<f', .1))[0]
    neighbors = [struct.unpack('<f', struct.pack('<I', first+k))[0] for k in (-1, 0, 1)]
    omegas = [(v, 0., 0.) for v in [0., .05]+neighbors+[.5, 1., 2., 5., 20.]]
    omegas += [(0., -v, 0.) for v in (.1, .5, 2., 20.)]
    omegas += [(0., 0., v) for v in (.1, .5, 2., 20.)]
    omegas += [(1., -2., 3.), (-.25, .5, -.75)]
    contexts, expected = [], []
    for omega, mass, wheels, state in itertools.product(omegas, (.8, 1.22, 1.85, 3., 6.5), range(5), range(3)):
        omega = tuple(f32(v) for v in omega)
        mass = f32(mass)
        wi(physics+0x34, car); wi(physics+0x20, body)
        wi(car+0x38, state); wi(car+0x3cc, wheels)
        wf(body+0x144, mass)
        for k, value in enumerate(omega):
            wf(body+0xc0+4*k, value); wf(body+0x120+4*k, 0.)
        u.reg_write(x.UC_X86_REG_EAX, physics)
        u.reg_write(x.UC_X86_REG_ESP, stack+0xf000)
        wi(stack+0xf000, 0)
        u.reg_write(x.UC_X86_REG_FPCW, 0x37f)
        u.reg_write(x.UC_X86_REG_FPSW, 0)
        u.reg_write(x.UC_X86_REG_FPTAG, 0xffff)
        u.emu_start(0x474dd0, 0, count=2000)
        if u.reg_read(x.UC_X86_REG_EIP) not in (0x474e8b, 0x47505e):
            raise RuntimeError('instruction budget exhausted')
        contexts.append(omega+(mass, wheels, state))
        expected.append(tuple(rf(body+0x120+4*k) for k in range(3)))
    text = ''.join(' '.join(str(v) for v in c)+'\n' for c in contexts)
    (out/'contexts.txt').write_text(text, encoding='ascii')
    (out/'native.json').write_text(json.dumps(expected)+'\n', encoding='ascii')
    run = subprocess.run([str(probe)], input=text, text=True, capture_output=True, check=True)
    (out/'port.txt').write_text(run.stdout, encoding='ascii')
    rows = [tuple(map(float, line.split())) for line in run.stdout.splitlines()]
    if run.stderr or len(rows) != len(expected) or any(len(r) != 3 for r in rows):
        raise ValueError('incomplete probe output')
    maximum = 0.
    for c, a, b in zip(contexts, expected, rows):
        for av, bv in zip(a, b):
            if not math.isfinite(bv) or abs(av-bv) > max(1e-5, abs(av)*2e-7):
                raise ValueError('native mismatch: %r %r %r' % (c, a, b))
            maximum = max(maximum, abs(av-bv))
    result = {'rows':len(rows), 'maximum_absolute_error':maximum,
              'table_sha256':hashlib.sha256(table).hexdigest().upper(),
              'exe_sha256':EXE_SHA, 'x87_control_word':'037F',
              'stop_before_alignment':True, 'reached_addresses':sorted(reached)}
    (out/'comparison.json').write_text(json.dumps(result, indent=2)+'\n', encoding='ascii')
    print('PASS native low-support torque: %d rows, max error %.9g, table %s' %
          (len(rows), maximum, result['table_sha256']))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'probe', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    forbidden = Path('E:/Games').resolve()
    if out == root or root in out.parents or out == forbidden or forbidden in out.parents:
        parser.error('output must be outside retail root and E:/Games')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out/'before.json').write_bytes(before)
    try:
        compare(root, out, args.probe.resolve())
    finally:
        after = manifest(root)
        (out/'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail tree changed')
        print('PASS retail manifests identical')
if __name__ == '__main__':
    main()