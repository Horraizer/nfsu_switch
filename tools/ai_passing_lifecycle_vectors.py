"""Execute original action 1 factory argument setup and lifecycle predicates."""
from hashlib import sha256
from pathlib import Path

from physics256_native import ACTOR, STACK, Machine, bits
from world_body_trace import Image

def main():
    import argparse, json
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):
        ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args()
    data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    image=Image(data);rows=[]
    allocation,context,target=0x1150000,0x1153000,0x1154000
    for start in (100000,0xfffff000):
      for found in (0,1):
        for side in (0,1):
          for collision in (.465245426,3.9):
            m=Machine(image)
            m.allowed=[(0x44DB43,0x44DB6D),(0x44F750,0x44F7C7),
                       (0x44E2B0,0x44E335),(0x44A080,0x44A0D6),
                       (0x674898,0x67490D),(0x458F10,0x4590A0),
                       (0x44F700,0x44F741),(0x452D10,0x452D20),
                       (0x44FAF0,0x44FB23),(0x44A0E0,0x44A103)]
            m.put(0x73AD34,start);m.put(STACK+0x14,bits(collision))
            m.put(STACK+0x18,300);m.put(STACK+0x20,side,'B');m.put(STACK+0x4C,found,'B')
            m.run(0x44DB43,end=0x44DB6D,eax=allocation,edi=target,ebx=0)
            assert m.get(allocation)==0x6C9324 and m.get(allocation+0x200)==target
            assert m.get(allocation+0x204,'f')==float(found)
            assert m.get(allocation+0x208,'B')==side and m.get(allocation+0x218,'B')==0
            for delta in (0,7999,8000,8001,9957,11999,12000,12001,12661):
                m.put(0x73AD34,(start+delta)&0xffffffff)
                m.run(0x44F700,esi=allocation,edi=context)
                status=m.get(allocation+0x10)
                m.run(0x44A0E0,ecx=allocation)
                complete=m.reg('eax')&255
                assert complete==status and not m.events
                rows.append(dict(start=start,found=bool(found),side=bool(side),collision=collision,
                                 delta=delta,complete=bool(complete),deadline=m.get(allocation+0x214,'f')))
    # Side admission and collision prediction are constructor inputs, but do
    # not change the native deadline. The connected fixture varies geometry.
    unique={}
    for r in rows:
        key=r['start'],r['found'],r['delta']
        if key in unique:assert unique[key]['complete']==r['complete']
        else:unique[key]=r
    lines=['// Original44DB43 factory tail,44F750 and44F700/44A0E0; no semantic hooks.',
           '// speed.exe SHA256 '+identity]
    for r in unique.values():
        lines.append('{%du,%s,%du,%s},'%(r['start'],str(r['found']).lower(),r['delta'],str(r['complete']).lower()))
    args.output.write_text('\n'.join(lines)+'\n','ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',
        semantic_hooks=0,cases=rows),indent=2)+'\n','ascii')
    print('Original passing lifecycle cases:',len(rows),'distinct predicates:',len(unique),'semantic hooks: 0')

if __name__=='__main__':main()
