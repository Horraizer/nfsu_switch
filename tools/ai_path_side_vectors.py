"""Original45C0D0 ordinary side admission; explicit zero-curvature service."""
import argparse,hashlib,json,struct
from pathlib import Path
from physics256_native import Machine
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    rows=[];cases=[]
    inputs=[(x,y,d,lo,hi) for x,y,d,lo,hi in [
        (0,0,20,-20,20),(0,0,9,-20,20),(0,0,9.000001,-20,20),
        (0,2,20,-20,20),(0,1.9999999,20,-20,20),(3,0,20,-20,20),
        (0,0,20,-6,20),(0,0,20,-20,6),(0,0,20,-6,6),
        (0,0,20,-7,7),(0,0,-3.9999998,-20,20),(0,0,-4,-20,20),
        (0,0,-4.000001,-20,20),(0,-3,4,-20,20)]]
    for x,y,d,lo,hi in inputs:
        m=Machine(Image(data));m.allowed=[(0x45c0d0,0x45c364),(0x456780,0x456824),(0x647b70,0x647b81)]
        ctx,actor,chain,cell,node,obj=0x1100000,0x1110000,0x1120000,0x1130000,0x1140000,0x1150000
        m.put(ctx,actor);m.put(ctx+0x388,0,'B');m.put(ctx+0x394,2);m.put(ctx+0x3a8,chain)
        m.put(actor+0x14,actor+0x900);m.put(actor+0x904,2);m.put(actor+0x82c,node+0x800)
        m.put(chain+8,cell);m.put(cell+0x20,lo,'f');m.put(cell+0x3c,hi,'f')
        for i,lat,dist in ((0,x,0),(1,y,d)):
            at=ctx+8+i*0x38
            for offset,value in ((8,1),(12,2),(16,dist),(20,lat)):m.put(at+offset,float(value),'f')
            m.put(at+0x30,0);m.put(at+0x34,node)
        m.put(node+0x1d4,obj);m.put(obj+0x24,6);m.put(obj+0x90,actor)
        m.scalar_hook(0x5a6860,'supplied_zero_curvature',pop=8)
        m.run(0x45c0d0,(0,ctx+8+0x38),edi=ctx)
        side=m.reg('eax')
        # Eligibility is checked separately through the full original routine.
        m.run(0x456780,(ctx+8+0x38,),esi=ctx)
        admitted=bool(m.reg('eax')&255)
        rows.append('{%s,%s,%s,%s,%s,%s,%du},'%(*map(literal,map(float,(x,y,d,lo,hi))),str(admitted).lower(),side))
        cases.append(dict(input=[x,y,d,lo,hi],side=side,eligible=admitted,hooks=m.events))
    a.output.write_text('// Original456780 and45C0D0, ordinary racer, zero curvature.\n'+'\n'.join(rows)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(sha256=sha,scope=__doc__,cases=cases),indent=2)+'\n',encoding='ascii')
    print('Original neighbor eligibility and side policy:',len(cases),'cases')
if __name__=='__main__':main()
