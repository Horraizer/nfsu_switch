"""Bounded original prop predicates/writer on synthetic contacts, not live play.

Contact lookup, candidate-list maintenance and retention branch endpoints
are explicit boundary stubs.
The writer refresh walker has an empty supplied component list; its decision
and momentum stores execute unchanged. No original file is modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from physics256_native import Machine
from track_prop_trace import Image, INPUTS

def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def neighbor(x,delta):
    bits=struct.unpack('<I',struct.pack('<f',x))[0]
    return struct.unpack('<f',struct.pack('<I',bits+delta))[0]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    out=(Path.cwd()/args.out).resolve()
    base=Path.cwd().resolve()
    if not out.is_relative_to(base) or out.exists():
        raise ValueError('output must be fresh under workspace')
    relative=out.relative_to(base)
    if not relative.parts or not relative.parts[0].startswith('build') or not str(relative).isascii():
        raise ValueError('output must use an ASCII build path')
    retail=args.root.resolve()
    if out.is_relative_to(retail) or retail.is_relative_to(out):
        raise ValueError('retail overlap refused')
    data=(args.root/'speed.exe').read_bytes()
    assert hashlib.sha256(data).hexdigest().upper()==INPUTS['speed.exe']
    image=Image(data);m=Machine(image)
    m.allowed=[(0x585E6C,0x585E96),(0x674898,0x67490D),(0x597EA0,0x597F94),(0x597FA0,0x5980CE),
               (0x420720,0x42074D),(0x597B80,0x597C65),(0x597C70,0x597C8B),(0x46ABD0,0x46AC08)]
    m.put(0x73457C,f32(1/60),'f');m.put(0x73AD34,10000)
    m.run(0x585E6C,end=0x585E96)
    assert m.get(0x73AD34)==10067
    body,rigid,record=0x1100000,0x1101000,0x1102000
    m.hook(0x468820,'supplied_first_contact',pop=4,value=record)
    m.hook(0x440740,'candidate_list_maintenance',value=0)
    m.hook(0x420840,'retention_keep_boundary',value=0)
    m.hook(0x42074D,'retention_expire_boundary',value=1)
    values=[0.,.05]+[neighbor(x,d) for x in (.1,.8,2.) for d in (-1,0,1)]
    velocities=[([v,0.,0.],[0.,0.,0.]) for v in values]
    velocities += [([0.,0.,0.],[v,0.,0.]) for v in values]
    velocities += [([.06,.08,0.],[.3,-.4,.5]),([.48,.64,0.],[0.,0.,0.])]
    rows=[]
    for v,w in velocities:
      for contact in (0,1):
       for previous in (0,2):
        for ticks,contact_ticks in ((0,0),(399,99),(400,100),(401,101),(1000,1000),(1000,1199),(1000,1200),(1000,1201)):
            m.uc.mem_write(body,bytes(0x600));m.uc.mem_write(rigid,bytes(0x200))
            m.put(body+0x6C,rigid);m.put(body+0x14C,body+0x14C)
            m.put(body+0x1C,contact,'B');m.put(body+0x4C4,previous)
            m.put(0x73AD34,10000);m.put(body+0x16C,10000-ticks)
            m.put(record+8,10000-contact_ticks)
            for i,x in enumerate(v):m.put(rigid+0xA0+4*i,x,'f')
            for i,x in enumerate(w):m.put(rigid+0xC0+4*i,x,'f')
            for i,x in enumerate((-3.25,2.75,f32(.123))):m.put(rigid+0x70+4*i,x,'f')
            for i,x in enumerate((7.5,-.375,2.)):m.put(rigid+0x80+4*i,x,'f')
            m.run(0x597FA0,ecx=body+0x140)
            state=m.get(body+0x4C4)
            momenta=[m.get(rigid+base+4*i) for base in (0x70,0x80) for i in range(3)]
            assert m.get(rigid+0x140)==int(state!=2)
            m.put(body+0x4C2,1,'B');m.put(0x73446C,1)
            m.run(0x597B80,eax=body)
            settle=int(m.get(body+0x4C2,'B')==0)
            age=f32(ticks*f32(.00025));contact_age=f32(contact_ticks*f32(.00025))
            m.put(record+0xC,10000-contact_ticks)
            m.run(0x420720,ecx=record)
            expired=m.reg('eax')
            rows.append(v+w+[contact,previous,age,contact_age,state,settle,expired]+momenta)
    out.mkdir(parents=True)
    (out/'vectors.txt').write_text(''.join(' '.join(str(x) for x in row)+'\n' for row in rows),encoding='ascii')
    report=dict(exe_sha256=INPUTS['speed.exe'],cases=len(rows),
        executed=['585E6C','420720','597FA0','597EA0','597B80','597C70','46ABD0'],
        boundary_stubs=['supplied_first_contact','candidate_list_maintenance','retention_branch_endpoints'],
        limits='Synthetic velocity/contact/clock inputs; empty component refresh list; no live contact retention or trajectory proof',
        reached=['%08X'%x for x in sorted(m.visited)])
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
    assert hashlib.sha256((args.root/'speed.exe').read_bytes()).hexdigest().upper()==INPUTS['speed.exe']
    print('PASS original prop predicates/momentum writer:',len(rows),'completed cases; explicit synthetic contact boundaries')
if __name__=='__main__':main()
