"""Execute original driver45E970 with real contact traversal, without hooks."""
import argparse
import hashlib
import json
from pathlib import Path

from world_body_trace import Image
from controller293_native import fixture
from physics256_native import ACTOR, DRIVER, PHYSICS, bits


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if args.out.exists():
        raise ValueError('fresh output required')
    data = (args.root/'speed.exe').read_bytes()
    identity = hashlib.sha256(data).hexdigest()
    if identity != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    image = Image(data)
    cases = []
    for side in (-1, 1):
        for selector, grounded, up, age, magnitude, endpoint in (
            (1, True, .05, 799, 11., 0), (1, True, -.05, 799, 11., 1),
            (1, True, .1, 799, 11., 0), (1, True, -.1, 799, 11., 1),
            (1, True, -.4, 799, 11., 0), (1, True, .05, 800, 11., 0),
            (1, True, .05, 799, 10., 0), (1, True, .05, -1, 11., 0),
            (3, True, .05, 799, 11., 0), (1, False, .05, 799, 11., 0),
        ):
            points = [(10., side*.01, 0.), (100., side*.06, 0.)]
            m = fixture(image, points, speed=20., target=30., category=1, grounded=grounded)
            m.allowed += [(0x468820, 0x46889d), (0x421970, 0x42197d)]
            own, plane, shape, pair, record = (0x1160000+i*0x1000 for i in range(5))
            m.put(ACTOR+0x28, selector)
            m.put(PHYSICS+0x48, 0x1165000) # Cold full-physics engine, heat+8C=0.
            m.put(ACTOR+0x3c, own)
            m.put(own+0x1c, 1, 'b')
            m.put(plane+0x15, 3, 'B')
            m.put(plane+0x60, shape)
            m.put(shape+0x78, up, 'f')
            ends = (plane, own) if endpoint == 0 else (own, plane)
            m.put(0x779be0, pair+0x250)
            m.put(pair+0x250, 0x779be0)
            m.put(pair+0x258, ends[0])
            m.put(pair+0x25c, ends[1])
            m.put(pair+0x264, record)
            m.put(record+0x64, pair)
            m.put(record+0x60, magnitude, 'f')
            frames = []
            for step in range(8):
                # Keep the supplied contact age on its exact boundary.
                now = 100000+step*67
                m.put(0x73ad34, now)
                m.put(record+0xc, now-age)
                m.run(0x45e970, (bits(1/60),), ecx=DRIVER)
                frames.append([m.get(DRIVER+0x1d0, 'H'), m.get(DRIVER+0x1dc), m.get(DRIVER+0x1e4)])
            if m.events:
                raise ValueError('unexpected native hook')
            cases.append([side, selector, grounded, up, age, magnitude, endpoint, frames])
    args.out.mkdir(parents=True)
    lines = ['// Original45E970; speed.exe '+identity,
             '// ai_driver_contact_vectors.py; original contact-list traversal; no hooks.']
    for side, selector, grounded, up, age, magnitude, endpoint, frames in cases:
        lines.append('{%d,%d,%s,%sF,%d,%sF, {' %
                     (side, selector, str(grounded).lower(), float(up).hex(), age, float(magnitude).hex()))
        lines += ['    {%d,0x%08Xu,0x%08Xu},' % tuple(frame) for frame in frames]
        lines.append('}},')
    (args.out/'pins.inc').write_text('\n'.join(lines)+'\n', 'ascii')
    (args.out/'original.json').write_text(json.dumps(dict(sha256=identity, cases=cases), indent=2)+'\n', 'ascii')
    print('PASS: %d original contact driver sequences, %d frames; no hooks' % (len(cases),len(cases)*8))


if __name__ == '__main__':
    main()
