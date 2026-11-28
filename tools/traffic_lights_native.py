"""Verify the original traffic-owner light gains; retail inputs are read-only.

The actual56D9B0..56DAA5 branch and5693B0 headlamp getter execute with
explicit owner/config/driver state. No hooks or native rendering are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from hud254_trace import PE
from physics256_native import Machine, ACTOR, DRIVER, CONFIG, STACK, bits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--write-fixture', action='store_true')
    args = parser.parse_args()
    raw = (args.root / 'speed.exe').read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    if args.out:
        if args.out.exists():
            raise ValueError('refusing to overwrite earlier evidence')
        args.out.mkdir(parents=True)
    image = PE(raw)
    rows = []
    threshold = struct.unpack('<I', image.read(0x6b5db0, 4))[0]
    samples = [0, bits(.001), threshold-1, threshold, threshold+1,
               bits(.06), bits(.25), bits(1.)]
    for kind in (1, 2, 3):
        for brake in samples:
            m = Machine(image)
            m.allowed = [(0x56d9b0, 0x56daa5), (0x5693b0, 0x5693c3)]
            m.put(ACTOR+0x14, CONFIG)
            m.put(CONFIG+4, kind)
            m.put(STACK+0x2c, ACTOR)
            m.put(STACK+0x28, DRIVER)
            m.put(DRIVER+0x1e4, brake)
            m.run(0x56d9b0, end=0x56daa5, eax=ACTOR, ebx=0)
            if m.events:
                raise AssertionError('unexpected external hook')
            rows.append([kind, brake, m.get(STACK+0x14), m.get(STACK+0x1c)])
    text = '// Original56D9B0..56DAA5 and5693B0; supplied owner and analog brake, no hooks.\n'
    text += 'static constexpr NativeTrafficLightCase native_traffic_lights[]={\n'
    for row in rows:
        text += ' {' + ','.join('0x%08XU' % value for value in row) + '},\n'
    text += '};\n'
    if args.out:
        (args.out/'traffic_lights_native.inc').write_text(text, encoding='ascii')
    fixture = Path.cwd()/'tests/traffic_lights_native.inc'
    if args.write_fixture:
        fixture.write_text(text, encoding='ascii')
    elif fixture.read_text(encoding='ascii') != text:
        raise AssertionError('original traffic light fixture differs')
    report = dict(executable_sha256=digest, rows=rows, hooks=[],
                  scope='Supplied-state owner gain branch and real headlamp getter; '
                        'does not execute mesh texture changes, geometry or a running game')
    if args.out:
        (args.out/'oracle.json').write_text(json.dumps(report, indent=2)+'\n', encoding='ascii')
    print('PASS original traffic light owner gains: %d cases; no hooks' % len(rows))


if __name__ == '__main__':
    main()
