"""Original45C760 geometry on initialized roads and explicit current actor snapshots.
No geometric result hooks. Supplied target path/cursor is a separate owner epoch.
"""
import argparse,hashlib,json
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dispatch-output',type=Path,required=True)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(a.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=list(EXEC_RANGES)+[(0x45c760,0x45cafd),(0x45cb00,0x45ce94),(0x45cea0,0x45cfa1),
        (0x45c370,0x45c4fa),(0x45c500,0x45c54a),(0x45c640,0x45c75e),(0x5a6310,0x5a65a4),(0x5a7270,0x5a733f),
        (0x4594d0,0x4596a6),(0x4596b0,0x459710),(0x459710,0x459865),
        (0x57b480,0x57b4d7),(0x44d5f0,0x44d637),(0x57a6b0,0x57a6e2),
        (0x57a720,0x57a752),(0x57a8e0,0x57a903),(0x5a7560,0x5a75b2),
        (0x401c40,0x401c59),(0x45ae10,0x45afce),(0x45ad30,0x45adc4),
        (0x458f10,0x459094),(0x45a7d0,0x45a8b2),(0x45a8c0,0x45a918),
        (0x45c550,0x45c636),(0x57b520,0x57b550)]+[(p,p+n) for p,n in [(0x57b358,0x11),(0x44ace8,6),(0x44d0cf,0x24),(0x5a7007,0xd)]]
    for begin,end in ranges:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(begin,end-begin),begin)})
    pair.actor.pose(30,20.);m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
    chain=m.get(CTX+0x3a8);selected=m.get(CTX+0x388,'b');entry=CTX+8+selected*56
    self_saved=bytes(m.uc.mem_read(entry,56));selfcell=m.get(entry+48)
    actor=[m.get(ACTOR+v,'f') for v in (0x60,0x64,0x68,0x70,0x74,0x78,0x90,0x94,0x98,0x3d4)]
    node=m.get(0x73b07c);other=0
    while node!=0x73b07c:
        if m.get(node+0x1d4)==m.get(HUMAN+0x82c):other=node
        node=m.get(node)
    if not other:raise ValueError('missing target world node')
    ref=m.get(other+0x1d4);body=m.get(ref+0x1c);config=m.get(HUMAN+0x14)
    descriptor=0x1150400;path=0x1150000
    # Declared initialized target controller; PairedController does not own one.
    m.put(HUMAN+0xc,0x1151000)
    # Preserve an original road-following target path as the declared incoming path.
    m.uc.mem_write(path,bytes(0x1c0));m.run(0x45c370,(path,CTX,bits(1.),2,bits(0.),bits(99999.)),budget=3000000)
    target_path=bytes(m.uc.mem_read(path,0x1c0))
    policy=[m.get(v,'f') for v in (0x6b74f8,0x6b74fc,0x6b7500,0x6b7504,0x6b7508,0x6b750c,0x6b7510,0x6b74f4)]
    margin=m.get(0x700638,'f');rows=[];dispatch=[];events=len(m.events)
    cases=[(side,role,extra,lat,2,speed,False) for side in (2,3) for role in (1,2,3)
           for extra,lat,speed in [(False,0.,20.),(True,2.,20.),(False,0.,0.)]]
    cases += [(2,1,False,100.,2,20.,False),(3,1,False,-100.,2,20.,False),
              (2,3,False,0.,3,20.,False),(3,1,False,0.,2,20.,True)]
    for category in (0,11,16):
      slot=0 if category<5 else category-10
      roads=[]
      for j in range(m.get(chain+4)):
        cell=m.get(chain+8+j*4);route=m.get(cell+0x5c,'b');index=m.get(cell+0x5e,'h')
        if m.get(cell)!=0x6c8f90:raise ValueError('nonforward cell')
        raw=m.get(0x77af30+4*route)+0x27c+index*0x88
        vals=[m.get(raw+v,'f') for v in (0,4,8,12,16,0x2c,0x30)]
        vals += [m.get(raw+0x3c+2*category,'h')/256,m.get(cell+4+slot*4,'f'),
                 m.get(cell+0x20+slot*4,'f'),m.get(cell+0x3c+slot*4,'f'),
                 m.get(raw+0x24,'f'),m.get(raw+0x28,'f'),m.get(cell+0x58,'f'),
                 m.get(raw+0x3c,'h')/256,m.get(cell+4,'f'),m.get(cell+0x20,'f'),m.get(cell+0x3c,'f')]
        roads.append([vals,route,index])
      targetcell=min(selfcell+4,len(roads)-1);r=roads[targetcell][0]
      for side,role,extra,lat,selfrole,speed,alternate in cases:
        m.put(CTX+4,2);m.put(CTX+0x388,0,'b');m.put(CTX+0x394,category,'i')
        m.uc.mem_write(CTX+8,self_saved);m.uc.mem_write(CTX+64,bytes(56))
        m.put(CTX+8+8,1.,'f');m.put(CTX+64+8,1.,'f')
        m.put(CTX+64+16,r[13],'f');m.put(CTX+64+48,targetcell);m.put(CTX+64+52,other)
        for at in (20,24,28,32,36,40,44):
            m.put(CTX+8+at,0.,'f');m.put(CTX+64+at,lat,'f')
        m.put(m.get(ACTOR+0x14)+4,selfrole);m.put(config+4,role);m.put(m.get(HUMAN+0xc)+0xc54,int(alternate))
        m.put(ref+0x24,6);m.put(body+0xe4,0)
        position=[r[0]+r[4]*(r[14]+r[15]+lat),r[1]-r[3]*(r[14]+r[15]+lat),r[2]]
        velocity=[r[3]*speed,r[4]*speed,0.]
        for k,v in enumerate(position):m.put(body+0x40+4*k,v,'f')
        for k,v in enumerate(velocity):m.put(body+0x90+4*k,v,'f')
        m.uc.mem_write(other+0x10,target_path)
        m.run(0x4596b0,(other+0x10,other),budget=1000000);cursor=m.reg('eax')
        m.put(descriptor,side);m.put(descriptor+4,other);m.put(descriptor+8,0.,'f');m.put(descriptor+12,int(extra),'B')
        m.uc.mem_write(path,bytes(0x1c0))
        m.run(0x45c760,(descriptor,path,CTX),budget=3000000)
        chosen=bool(m.reg('eax')&255);size=m.get(path,'B');length=m.get(path+0x1ac)
        points=[[[m.get(path+16+16*j+4*k) for k in range(3)],m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(size)]
        target_points=[[m.get(other+0x20+16*j+4*k,'f') for k in range(3)] for j in range(m.get(other+0x10,'B'))]
        rows.append([category,side,extra,selfrole,role,alternate,margin,policy,actor,position,velocity,
            selfcell,targetcell,m.get(CTX+8+16,'f'),r[13],lat,cursor,len(target_points),target_points,
            len(roads),roads,chosen,bool(m.get(path+0x1b1,'B')),m.get(path+0x1b0,'B'),size,length,points])
        # Exact45AE10 wrapper, supplied zero-prefix rebuild and retained states.
        # Strategy is absent; original controller4BC selects rebuild directly.
        m.put(CONTROLLER+0x3fc,0)
        if m.get(0x6b74b8,'f')!=0:raise ValueError('nonzero original prefix setting')
        params=[m.get(v,'f') for v in (0x77a4a8,0x6b7520,0x6b7524)]
        for rebuild in (False,True):
            m.put(CONTROLLER+0x4bc,int(rebuild),'B')
            previous=m.get(ACTOR+0x30)+0x10
            m.uc.mem_write(previous,target_path)
            previous_points=[[[m.get(previous+16+16*j+4*k) for k in range(3)],m.get(previous+0x150+j,'B'),m.get(previous+0x164+2*j,'H')] for j in range(m.get(previous,'B'))]
            previous_record=[m.get(previous,'B'),m.get(previous+0x1ac),previous_points]
            m.uc.mem_write(path,bytes(0x1c0))
            m.run(0x45ad30,eax=CTX,ecx=descriptor,budget=300000)
            admitted=bool(m.reg('eax')&255)
            m.run(0x45ae10,(descriptor,path,CTX,0,2),budget=3000000)
            chosen=bool(m.reg('eax')&255);size=m.get(path,'B')
            points=[[[m.get(path+16+16*j+4*k) for k in range(3)],m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(size)]
            dispatch.append([len(rows)-1,rebuild,admitted,m.get(0x73ad34),params,
                m.get(ACTOR+0x3d0,'f'),chosen,bool(m.get(path+0x1b1,'B')),m.get(path+0x1b0,'B'),
                size,m.get(path+0x1ac),points,previous_record])
    hooks=m.events[events:]
    if hooks:raise ValueError('unexpected native geometry hook: '+str(hooks[:3]))
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    a.output.write_text('// Original45C760/45CEA0/45CB00, current supplied bodies/target path. No query hooks.\n'+
        '\n'.join(lit(row)+',' for row in rows)+'\n',encoding='ascii')
    a.dispatch_output.write_text('// Original45AE10 wrapper with original maneuver geometry; no hooks.\n'+\
        '\n'.join(lit(row)+',' for row in dispatch)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',cases=rows,dispatch=dispatch,
        hooks=hooks,scope='Initialized retail roads, supplied actor and target path epoch; no live collision or full race claim'),indent=2)+'\n',encoding='ascii')
    print('Original action dispatch:',len(dispatch),'cases; no query hooks')
    print('Original maneuver geometry:',len(rows),'cases;',sum(r[-3] for r in rows),'points; no query hooks')
if __name__=='__main__':main()
