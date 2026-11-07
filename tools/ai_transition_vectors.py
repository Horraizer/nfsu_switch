"""Original5A6310 on initialized retail roads; no path/query hooks."""
import argparse,hashlib,json
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=(list(EXEC_RANGES)+[(0x5a6310,0x5a65a4),(0x4594d0,0x4596a6),(0x5a7270,0x5a733f)]+
        [(a,a+n) for a,n in ((0x57b358,0x11),(0x44ace8,6),(0x44d0cf,0x24),(0x5a7007,0xd))])
    for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    pair.actor.pose(30,20.);m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
    chain=m.get(CTX+0x3a8);selected=m.get(CTX+0x388,'b');entry=CTX+8+selected*56
    actor=[m.get(ACTOR+a,'f') for a in (0x60,0x64,0x68,0x70,0x74,0x78,0x90,0x94,0x98,0x3d4)]
    margin=m.get(0x700638,'f');rows=[];lines=['// Unchanged original5A6310; initialized retail roads; no query hooks.']
    first_event=len(m.events);path=0x1150000
    for category,start,end,source,dest,width,extra in [
        (0,1,8,0.,3.,1.,False),(0,1,8,3.,-3.,1.,False),
        (0,1,12,-3.,4.,1.,True),(0,1,8,0.,100.,1.,True),
        (0,1,8,0.,-100.,1.,True),(0,1,1,0.,3.,1.,False),
        (11,1,8,0.,3.,1.,True),(16,1,8,3.,-3.,1.,False),
        (0,-2,8,0.,2.,1.,False),(0,50,60,2.,0.,1.,False)]:
        slot=0 if category<5 else category-10;roads=[]
        for j in range(m.get(chain+4)):
            cell=m.get(chain+8+4*j)
            if m.get(cell)!=0x6c8f90:raise ValueError('nonforward road')
            route=m.get(cell+0x5c,'b');index=m.get(cell+0x5e,'h')
            raw=m.get(0x77af30+4*route)+0x27c+index*0x88
            roads.append([[m.get(raw+a,'f') for a in (0,4,8,12,16,0x2c,0x30)]+
                [m.get(raw+0x3c+2*category,'h')/256,m.get(cell+4+4*slot,'f'),
                 m.get(cell+0x20+4*slot,'f'),m.get(cell+0x3c+4*slot,'f'),
                 m.get(raw+0x24,'f'),m.get(raw+0x28,'f'),m.get(cell+0x58,'f')],route,index])
        m.put(entry+8,width,'f');m.uc.mem_write(path,bytes(0x1c0))
        m.run(0x5a6310,(chain,path,bits(source),bits(dest),category,int(extra)),
              eax=CTX,ecx=start,edx=end,budget=1000000)
        count=m.get(path,'B');length=m.get(path+0x1ac);limited=bool(m.get(path+0x1b1,'B'))
        points=[[[m.get(path+16+16*j+4*k) for k in range(3)],m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(count)]
        rows.append([category,start,end,source,dest,width,margin,extra,actor,len(roads),roads,count,length,points,limited])
    events=m.events[first_event:]
    if events:raise ValueError('unexpected transition hooks')
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    args.output.write_text('\n'.join(lines+[lit(v)+',' for v in rows])+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',
        cases=rows,query_hooks=events),indent=2)+'\n',encoding='ascii')
    print('Original transition:',len(rows),'cases,',sum(r[-4] for r in rows),'emitted points, no query hooks')
if __name__=='__main__':main()
