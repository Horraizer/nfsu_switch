"""Original427A30 retained speed/gear/RPM publication, without hooks."""
import argparse
import hashlib
import json
from pathlib import Path

from world_body_trace import Image
from physics256_native import Machine, ACTOR


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    if args.out.exists():raise ValueError('fresh output required')
    data=(args.root/'speed.exe').read_bytes()
    sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    image=Image(data)
    rows=[]
    for speed in (-3.,-2.,0.,1.,25.379997,25.3799991607666,25.380002,
                  40.88999938964844,56.470001220703125,72.05999755859375,
                  80.,85.,89.51000213623047,100.):
        for previous in (0.,speed):
            rows.append((speed,previous,2,4000.,0.,100.))
    rows += [(20.,20.,2,1000.,0.,100.),(20.,20.,2,8000.,0.,100.),
             (50.,50.,4,1000.,0.,100.),(50.,50.,4,9000.,0.,100.),
             (30.,60.,5,7000.,10.,100.),(30.,60.,5,7000.,0.,5000.),
             (120.,120.,6,8000.,0.,100.)]
    results=[]
    for speed,previous,gear,rpm,forced_speed,forced_rpm in rows:
        m=Machine(image);m.allowed=[(0x427a30,0x427c42)]
        for at,value in ((ACTOR+0x3d4,speed),(ACTOR+0x4c0,previous),(ACTOR+0x3e0,rpm),
                         (0x7361ec,forced_speed),(0x7000e4,forced_rpm)):
            m.put(at,value,'f')
        m.put(ACTOR+0x3d8,gear)
        inputs=[m.get(at,'f') for at in (ACTOR+0x3d4,ACTOR+0x4c0,ACTOR+0x3e0,0x7361ec,0x7000e4)]
        m.run(0x427a30,esi=ACTOR)
        if m.events:raise ValueError('unexpected hook')
        results.append([*inputs,gear,m.get(ACTOR+0x4c0),m.get(ACTOR+0x3d8),m.get(ACTOR+0x3e0)])
    args.out.mkdir(parents=True)
    lines=['// Original427A30; speed.exe '+sha,
           '// ai_path_powertrain_vectors.py; untouched routine, no hooks.']
    for row in results:
        lines.append('{'+','.join(float(v).hex()+'F' for v in row[:5])+',%d,0x%08Xu,%d,0x%08Xu},'%tuple(row[5:]))
    (args.out/'pins.inc').write_text('\n'.join(lines)+'\n','ascii')
    (args.out/'original.json').write_text(json.dumps(dict(sha256=sha,cases=results),indent=2)+'\n','ascii')
    print('PASS: %d original path powertrain cases; no hooks'%len(results))


if __name__=='__main__':main()
