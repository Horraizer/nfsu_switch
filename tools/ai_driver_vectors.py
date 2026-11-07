"""Print bounded original 45E970 stateful driver vectors; retail is read-only.

Run with standalone Python, Unicorn and Capstone. The caller redirects stdout
to a fresh scratch file and inspects it before updating any tracked test pin.
These are supplied-state oracle cases, not original gameplay trajectories.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn import UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EIP
from world_body_trace import Image
from controller293_native import fixture
from physics256_native import ACTOR, DRIVER, RIDE, PHYSICS, bits


def f(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--air', action='store_true', help='loss of front/all support and recontact')
    parser.add_argument('--drag', action='store_true', help='selector1/controller2 drag state')
    args = parser.parse_args()
    data = (args.root / 'speed.exe').read_bytes()
    identity = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest() != identity:
        raise ValueError('unsupported retail executable')
    image = Image(data)
    lines = ['// Original speed.exe SHA256 ' + identity,
             '// tools/ai_driver_vectors.py; 007F x87, supplied-state 45E970.',
             '// Cases preserve original histories across sixteen calls.',
             '// drag=%s air=%s' % (args.drag, args.air)]
    for case in (0, 1, 2, 3, 8, 12, 17, 29, 71):
        speed = f((case % 9) * 10)
        category, heading, direction = case % 4, (case * 83) & 65535, case % 3 - 1
        points = [(10., direction * 10., 0.), (100., direction * 60., 0.)]
        m = fixture(image, points, speed=speed, category=category, heading=heading)
        def invalid_memory(uc, access, address, size, value, data):
            print('native access', hex(uc.reg_read(UC_X86_REG_EIP)), hex(address), flush=True)
            raise RuntimeError('unmapped native access at %08X to %08X' % (uc.reg_read(UC_X86_REG_EIP), address))
        m.uc.hook_add(UC_HOOK_MEM_INVALID, invalid_memory)
        if args.drag:
            m.put(ACTOR + 0x28, 1)
            m.put(ACTOR + 0x24, 2)
            m.put(ACTOR + 0x1c, 0x1154000)
            m.put(ACTOR + 0x3c, 0x1153000)
            m.put(0x78a344, 1, 'B')
            m.put(PHYSICS + 0x48, 0x1155000) # cold engine, heat +8c=0
        for j, value in enumerate((speed, 0., 0.)):
            m.put(ACTOR + 0x70 + 4*j, value, 'f')
        for j, value in enumerate((1, 1, case % 2, case % 2)):
            m.put(ACTOR + 0x350 + j, value, 'B')
        m.put(RIDE + 0x1c0, (0., .5, 1.)[case % 3], 'f')
        if args.air:
            lines.append('// --air: frames4..7 rear only;8..11 no support;12..15 recontact.')
        lines.append('{%d, {' % case)
        for step in range(16):
            if args.air:
                contact = (0, 0, 1, 1) if 4 <= step < 8 else (0, 0, 0, 0) if 8 <= step < 12 else (1, 1, case % 2, case % 2)
                for j, value in enumerate(contact):
                    m.put(ACTOR + 0x350 + j, value, 'B')
            desired = f((0., 1., 2., 4.99, 5., 10., 30., 90.)[step // 2])
            for j, value in enumerate((f((step % 5 - 2)*.3), 0., f((case % 5 - 2)*.2), 5.)):
                m.put(ACTOR + 0x374 + 8*j, value, 'f')
            m.put(0x73ad34, 40000 + step*67)
            m.run(0x45e180, (bits(desired), 0, 0, int(step % 7 != 6)), ecx=DRIVER)
            m.run(0x45e970, (bits(f(1/60)),), ecx=DRIVER)
            extra = ''
            if args.air:
                history = m.get(DRIVER+0x23c)
                count = m.get(history+0x2e, 'B')
                integral = f(f(m.get(history+0x30, 'f')*count) /
                             f(m.get(history+8, 'f')*m.get(history+0x78, 'f'))) if count else 0.
                extra = ', 0x%08Xu, 0x%08Xu, 0x%08Xu' % (
                    m.get(history+0x7c), bits(integral), m.get(history+0x5c))
            lines.append('    {%d, 0x%08Xu, 0x%08Xu%s},' % (
                m.get(DRIVER+0x1d0, 'H'), m.get(DRIVER+0x1dc), m.get(DRIVER+0x1e4), extra))
        lines.append('}},')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
