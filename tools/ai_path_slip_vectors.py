"""Execute original464405..46445A with explicit path speed and actor motion."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from hud254_trace import PE
from physics256_native import Machine, STACK


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if args.out.exists():
        raise ValueError('fresh output required')
    data = (args.root/'speed.exe').read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    rows = []
    for axes in (((1,0,0),(0,1,0)), ((.96,0,-.28),(0,1,0)), ((.6,.8,0),(-.8,.6,0))):
        for speed, velocity in ((25,(25,3,0)), (30,(28,0,-2)), (0,(-3,4,1)), (25,(25,0,0))):
            m = Machine(PE(data))
            actor, path, trampoline = 0x1100000, 0x1101000, 0x1102000
            m.allowed = [(0x464405,0x46445e), (trampoline,trampoline+11)]
            m.put(path+8,actor)
            for i, axis in enumerate(axes):
                for j, v in enumerate(axis):m.put(actor+0x90+i*16+j*4,v,'f')
            m.put(STACK+0xc,speed,'f')
            for offset, v in zip((0x40,0x44,0x48),velocity):m.put(STACK+offset,v,'f')
            # Supply the source's live x87 Vz, then execute the untouched slice.
            m.uc.mem_write(trampoline,b'\xd9\x05'+struct.pack('<I',STACK+0x48)+
                           b'\xe9'+struct.pack('<i',0x464405-(trampoline+11)))
            m.run(trampoline,ebx=path,end=0x46445e)
            if m.events:raise ValueError('unexpected hook')
            values = [m.get(STACK+0xc,'f')]
            values += [m.get(actor+0x90+i*16+j*4,'f') for i in range(2) for j in range(3)]
            values += [m.get(STACK+offset,'f') for offset in (0x40,0x44,0x48)]
            values += [m.get(STACK+offset,'f') for offset in (0x18,0x24)]
            rows.append(values)
    args.out.mkdir(parents=True)
    (args.out/'pins.inc').write_text('// Original464405..46445A; speed.exe '+sha+'\n'+
        '\n'.join('{'+','.join(float(v).hex()+'F' for v in row)+'},' for row in rows)+'\n','ascii')
    (args.out/'original.json').write_text(json.dumps(dict(sha256=sha,cases=rows),indent=2)+'\n','ascii')
    print('PASS: %d original path-slip cases; no hooks' % len(rows))


if __name__ == '__main__':main()
