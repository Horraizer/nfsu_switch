"""Execute original584B10/584B80 on explicit published actor motion.

The original scalar matrix routines execute unchanged. This checks component
import, not the subsequent ground lookup or an entire race.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import contact256_native as n
from world_body_trace import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out.exists() or out == root or root in out.parents:
        raise ValueError('fresh non-retail output required')
    data = (root/'speed.exe').read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    image = Image(data)
    rows = []
    for sampled in (False, True):
        for mass, inertia in ((1.22, (.459553659, 2.24627399, 2.36746168)),
                              (1.4, (.731, 2.59, 3.134))):
            for angular in ((0., 0., 0.), (2., -3., 4.)):
                m = n.fixture(image, (4, 0, 0), (0, 0, 0), (0, 0, 0))
                rigid, props, actor = n.RIGID_A, 0x112f000, n.ACTOR_A
                m.put(rigid+0x14, props)
                m.put(props, mass, 'f')
                n.matrix(m, props+0x10)
                for offset, value in zip((0, 0x14, 0x28), inertia):
                    m.put(props+0x10+offset, value, 'f')
                m.put(props+0x50, .4, 'f')
                m.run(0x584b10, esi=rigid)
                c, s = math.cos(.7), math.sin(.7)
                axes = [[c, s, 0], [-s, c, 0], [0, 0, 1]]
                if sampled:
                    axes = [[.5*(v+(i == k)) for k, v in enumerate(a)] for i, a in enumerate(axes)]
                for k, axis in enumerate(axes):
                    n.vector(m, actor+0x90+16*k, axis)
                n.vector(m, actor+0x60, (1.125, -30.5, 12.))
                n.vector(m, actor+0x70, (31.75, -2.125, .5))
                n.vector(m, actor+0xd0, angular)
                row = dict(mass=m.get(props, 'f'),
                           inertia=[m.get(props+0x10+o, 'f') for o in (0, 0x14, 0x28)],
                           axes=[n.vector(m, actor+0x90+16*k) for k in range(3)],
                           position=n.vector(m, actor+0x60), velocity=n.vector(m, actor+0x70),
                           omega=n.vector(m, actor+0xd0))
                m.run(0x584b80, (actor+0x70, actor+0xd0),
                      eax=actor+0x60, ecx=actor+0x90, edx=rigid)
                if m.events:
                    raise ValueError('unexpected semantic hook')
                row.update(momentum=n.vector(m, rigid+0x70), angular=n.vector(m, rigid+0x80))
                rows.append(row)
    out.mkdir(parents=True)
    lines = ['// Original584B10/584B80; no semantic hooks.', '// speed.exe SHA256 '+sha]
    for r in rows:
        values = [r['mass'], *r['inertia'], *sum(r['axes'], []), *r['position'],
                  *r['velocity'], *r['omega'], *r['momentum'], *r['angular']]
        lines.append('{'+','.join(float(v).hex()+'F' for v in values)+'},')
    (out/'pins.inc').write_text('\n'.join(lines)+'\n', encoding='ascii')
    (out/'original.json').write_text(json.dumps(dict(executable_sha256=sha, cases=rows), indent=2)+'\n', encoding='ascii')
    print('PASS: %d original rigid reconstruction cases; no semantic hooks' % len(rows))


if __name__ == '__main__':
    main()
