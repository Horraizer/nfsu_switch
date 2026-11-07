"""Execute original role2 cadence and driver path publication on declared inputs.

Retail code stays read-only. Endpoint stops observe branches; no AI, geometry,
timer or allocation service is replaced. Generated fixtures contain numeric
results only, never executable or asset bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from world_body_trace import Image
from physics256_native import Machine, ACTOR, CONFIG


def gate(image,frame,tick,previous,slot,count,have):
    controller,world,action=0x1160000,0x1170000,0x1180000
    m=Machine(image)
    m.allowed=[(0x44b56d,0x44b5bd),(0x44b6d9,0x44b715)]
    m.put(controller+4,ACTOR);m.put(ACTOR+0x14,CONFIG);m.put(CONFIG+4,2)
    m.put(0x7361f8,world);m.put(world+0x1c,2);m.put(world+0x20,2+count)
    for i in range(count):
        m.put(world+0x28+(2+i)*4,ACTOR if i==slot else ACTOR+0x1000*(i+1))
    m.put(0x6f08a4,frame);m.put(0x73ad34,tick)
    m.put(controller+0x4c0,previous);m.put(controller+0x3f8,action if have else 0)
    retained=[]
    def stop(machine):
        retained.append(True);machine.finished=True;machine.uc.emu_stop()
    m.hook(0x44b715,'retained-branch-endpoint',callback=stop)
    m.run(0x44b56d,end=0x44b5fc,ebx=controller,budget=1000)
    return not retained


def publish(image,points):
    m=Machine(image);path=0x1150000
    m.allowed=[(0x459980,0x459abb),(0x459870,0x45997d),
               (0x583f20,0x583fe8),(0x4387e0,0x4387f7),(0x674898,0x67490e)]
    m.put(path,len(points),'B');m.put(path+0x1ac,77.,'f')
    m.put(path+0x1a0,2,'B');m.put(path+0x1a4,12345);m.put(path+0x1a8,11111)
    for i,p in enumerate(points):
        for k,v in enumerate(p):m.put(path+16+16*i+4*k,v,'f')
    m.run(0x459980,eax=path,budget=10000)
    return dict(points=[[m.get(path+16+16*i+4*k) for k in range(3)] for i in range(len(points))],
                length=m.get(path+0x1ac),cursor=m.get(path+0x1a0,'B'),
                cursor_tick=m.get(path+0x1a4),prediction_tick=m.get(path+0x1a8))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','cadence','publication','evidence'):
        p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();raw=(args.root/'speed.exe').read_bytes()
    sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('Unsupported executable')
    image=Image(raw);rows=[]
    for count in (1,2,3):
        for slot in range(count):
            for frame in range(16):
                for age in (799,800,801,1000):
                    for have in (False,True):
                        row=[frame,500000,500000-age,slot,count,have]
                        rows.append(row+[gate(image,*row)])
    args.cadence.write_text('// Original44B56D..44B70F ordinary rival cadence; endpoint observations only.\n'+
        ''.join('{'+','.join(str(v).lower() if isinstance(v,bool) else str(v)+'U' for v in r)+'},\n' for r in rows),encoding='ascii')
    cases=[[],[(3.,2.,1.)],[(3.,2.,1.),(4.,5.,6.)],[(0.,0.,0.)]*4,
           [(0.,0.,0.),(0.,0.,0.),(10.,0.,0.),(10.,10.,1.)],
           [(0.,0.,0.),(1.,0.,0.),(1.,11.,0.)],
           [(100.,-300.,24.),(101.,-303.,24.1),(101.1,-303.2,24.11),(125.,-310.,25.)],
           [(float(i*i),float(i%3)*7,float(i)*.25) for i in range(20)],
           # Owner capture seq26: nonuniform 3D lengths distinguish the
           # actual x87 sum order from a superficially equivalent expression.
           [(62.7750129699707,-214.9475555419922,23.230615615844727),
            (106.21888732910156,-237.9259490966797,22.15152359008789),
            (112.92252349853516,-240.8495635986328,21.974233627319336),
            (123.23065185546875,-246.35174560546875,21.68578338623047),
            (130.7017059326172,-250.52877807617188,21.47052764892578)]]
    publications=[];lines=['// Original459980/459870/583F20, 007F x87, no semantic hooks.']
    def literal(value):
        s=format(value,'.17g')
        if '.' not in s and 'e' not in s:s+='.'
        return s+'F'
    for points in cases:
        after=publish(image,points);publications.append(dict(points=points,after=after))
        lines.append('{%d,{%s},{%s},0x%08Xu,%du,%du,%du},'%(len(points),
            ','.join('{'+','.join(literal(v) for v in q)+'}' for q in points),
            ','.join('{'+','.join('0x%08Xu'%v for v in q)+'}' for q in after['points']),
            after['length'],after['cursor'],after['cursor_tick'],after['prediction_tick']))
    args.publication.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,cadence=rows,publication=publications,
        scope=__doc__),indent=2)+'\n',encoding='ascii')
    print('Original cadence cases',len(rows),'publication cases',len(publications))


if __name__=='__main__':main()
