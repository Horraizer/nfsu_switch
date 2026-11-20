"""Original59BAB0 and5A6EF0 on declared raw records and real cell vtables."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from physics256_native import Machine
from world_body_trace import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'output', 'evidence'):
        ap.add_argument('--'+key, type=Path, required=True)
    args = ap.parse_args()
    data = (args.root/'speed.exe').read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    im = Image(data)
    rows = []
    for category in (-1, 0, 4, 11, 16):
        for layout in range(4):
            for count, first in ((1, 0), (3, 0), (3, 1), (3, 2)):
                m = Machine(im)
                m.allowed = [(0x59bab0, 0x59badd), (0x5a6ef0, 0x5a6f4f),
                             (0x5a7690, 0x5a7700), (0x4134b0, 0x4134c0),
                             (0x4553d0, 0x455410)]
                route, chain, descriptor = 0x1100000, 0x1110000, 0x1120000
                store, result = 0x1130000, 0x1130100
                code = b'\xd9\x1d'+struct.pack('<I', result)+b'\xc3'
                m.uc.mem_write(store, code)
                m.allowed.append((store, store+len(code)))
                m.put(0x77af30, route)
                m.put(chain+4, count)
                m.put(descriptor, route)
                m.put(descriptor+4, route+0x27c)
                m.put(descriptor+8, category, 'i')
                profiles = []
                for j in range(3):
                    raw = route+0x27c+j*0x88
                    values = [37+j*5]+[(k*11+j*(7 if k%2 else -5)+30)%200 for k in range(23)]
                    m.uc.mem_write(raw+0x69, bytes(values))
                    cell = chain+0x200+j*0x100
                    reverse = bool((layout >> (j%2)) & 1)
                    m.put(chain+8+4*j, cell)
                    m.put(cell, 0x6c8f5c if reverse else 0x6c8f90)
                    m.put(cell+0x5c, 0, 'B')
                    m.put(cell+0x5e, j, 'h')
                    profiles.append(dict(reverse=reverse, values=values))
                m.run(0x59bab0, eax=descriptor)
                m.run(store)
                speed = m.get(result, 'f')
                m.run(0x5a6ef0, (chain, category & 0xffffffff), ecx=first)
                increasing = bool(m.reg('eax') & 255)
                if m.events:
                    raise ValueError('unexpected semantic hook')
                rows.append(dict(category=category, count=count, first=first,
                                 profiles=profiles, speed=speed, increasing=increasing,raw_present=True))
                m.put(descriptor,0)
                m.run(0x59bab0,eax=descriptor)
                m.run(store)
                rows.append(dict(category=category,count=count,first=first,profiles=profiles,
                                 speed=m.get(result,'f'),increasing=increasing,raw_present=False))
    lines = ['// Original59BAB0/5A6EF0 with real forward/reverse cell vtables; no semantic hooks.',
             '// speed.exe SHA256 '+sha]
    for r in rows:
        cells = ','.join('{%s,{%s}}' % (str(c['reverse']).lower(), ','.join(map(str, c['values'])))
                         for c in r['profiles'])
        lines.append('{%d,%d,%d,{%s},%d,%s,%s},' %
                     (r['category'], r['count'], r['first'], cells, int(r['speed']),
                      str(r['increasing']).lower(),str(r['raw_present']).lower()))
    args.output.write_text('\n'.join(lines)+'\n', encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha, semantic_hooks=0,
                                            cases=rows), indent=2)+'\n', encoding='ascii')
    print('PASS original nitrous raw speed/chain:', len(rows), 'cases; semantic hooks: 0')


if __name__ == '__main__':
    main()
