"""Read-only native588AB0/584E30 fixtures, including both impulse caps.

The synthetic x86 caller only stores the actual x87 return. No semantic hook
replaces a game function. Private body/strut/wheel operands are supplied; this
is a leaf comparison, not a claim about a complete original world frame.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from physics256_native import Machine, STOP
from world_body_trace import Image


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def fixtures():
    identity = (1, 0, 0, 0, 1, 0, 0, 0, 1)
    axes = (identity, (0, 1, 0, -1, 0, 0, 0, 0, 1),
            (.8, 0, -.6, 0, 1, 0, .6, 0, .8))
    for matrix in axes:
        for velocity in ((2, 1, -3), (2, 1, 3), (0, 0, 0),
                         (100, -200, -1000000)):
            for normal in ((0, 0, 1), (.36, .48, .8)):
                yield (1.22,) + matrix + (.1, -.2, .3) + (-.4, .5, -.6) + \
                    velocity + (.2, .3, .4) + (.9, .7, -.3) + normal
    yield (1.1,) + identity + (0,) * 6 + (0, 0, -50) + (0,) * 3 + \
        (100, -200, -.3) + (0, 0, 1)
    yield (2.,) + identity + (0,) * 6 + (0, 0, -4) + (0,) * 3 + \
        (0, 0, -.3) + (0, 0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail-root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source = args.retail_root / 'speed.exe'
    exe = source.read_bytes()
    digest = hashlib.sha256(exe).hexdigest()
    if digest != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported original executable')
    args.out.mkdir(parents=True, exist_ok=False)
    m = Machine(Image(exe))
    m.allowed = [(0x588AB0, 0x588BF5), (0x584E30, 0x584F60), (0x584B10, 0x584B3D),
                 (0x5ABDAF, 0x5ABF00), (STOP + 0x200, STOP + 0x240)]
    strut, body, wheel, output = 0x1100000, 0x1101000, 0x1102000, 0x1103000
    m.put(0x6D4AC4, 0x5ABDB5)
    m.put(strut + 0xA4, body)
    m.put(strut + 0xA0, wheel)
    m.put(0x734540, 0)  # Original default, impulse application enabled.
    entry = STOP + 0x200
    caller = b'\xe8' + struct.pack('<i', 0x588AB0 - (entry + 5))
    caller += b'\xd9\x1d' + struct.pack('<I', output) + b'\xc3'
    m.uc.mem_write(entry, caller)
    lines = []
    for values in fixtures():
        values = tuple(map(f32, values))
        mass, *data = values
        for k, value in enumerate(data[:9]):
            m.put(body + 0x30 + (k // 3) * 16 + (k % 3) * 4, value, 'f')
        for offset, vector in ((0x70, data[9:12]), (0x80, data[12:15]),
                               (0xA0, data[15:18]), (0xC0, data[18:21])):
            for k, value in enumerate(vector):
                m.put(body + offset + k * 4, value, 'f')
        for k, value in enumerate(data[21:24]):
            m.put(strut + 0x30 + k * 4, value, 'f')
        for k, value in enumerate(data[24:27]):
            m.put(wheel + 0x140 + k * 4, value, 'f')
        # Authenticate the mass/inverse mapping through the actual584B10
        # property-writer prefix, stopping before its inertia-tensor call.
        # Recorded Civic snapshots also have mass1.18 at144, not its inverse.
        setup = 0x1104000
        m.put(body + 0x14, setup)
        m.put(setup, mass, 'f')
        m.run(0x584B10, end=0x584B3D, esi=body)
        if m.get(body + 0x144, 'f') != mass or m.get(body + 0x148, 'f') != f32(1. / mass):
            raise RuntimeError('native mass/inverse property mapping differs')
        m.run(entry, esi=strut)
        result = [m.get(output, 'f')]
        result += [m.get(body + offset + k * 4, 'f')
                   for offset in (0x70, 0x80) for k in range(3)]
        row = values + tuple(result)
        lines.append('    {' + ','.join(
            '0x%08XU' % struct.unpack('<I', struct.pack('<f', v))[0]
            for v in row) + '},')
    if m.events:
        raise RuntimeError('unexpected native semantic hook')
    if source.read_bytes() != exe:
        raise RuntimeError('retail executable changed')
    (args.out / 'pins.inc').write_text('\n'.join(lines) + '\n', encoding='ascii')
    report = dict(exe_sha256=digest, cases=len(lines), hooks=m.events,
                  mass_property_prefix='584B10..584B3D;144=mass,148=inverse',
                  visited_addresses=len(m.visited), retail_unchanged=True)
    (args.out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='ascii')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
