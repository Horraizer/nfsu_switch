"""Original45BAE0 ordered following choice with declared geometry services.
This is a selection-control oracle, not a substitute for native geometry tests.
"""
import argparse,hashlib,json
from pathlib import Path
from physics256_native import Machine,bits
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):p.add_argument('--'+k,type=Path,required=True)
    a=p.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    target=0x1140000;cases=[]
    for current in ((1,0,0.,False),(1,0,2.,False),(2,target,0.,False),(3,target,0.,False)):
      for fast in (False,True):
       for policy in range(4):
        for room in (False,True):
         for accepted in (0,1,2,3):
          cases.append((current,fast,policy,room,accepted))
    evidence=[];lines=['// Original45BAE0; declared geometry, admission, room and side services.']
    for current,fast,policy,room,accepted in cases:
        m=Machine(Image(raw));m.allowed=[(0x45bae0,0x45bfb0),(0x44a000,0x44a051),(0x45add0,0x45ae10),(0x45cfb0,0x45cfeb),(0x458150,0x458214)]
        ctx,desc,path,side,actor,entry=0x1100000,0x1120000,0x1121000,0x1122000,0x1130000,0x1131000
        ref,body=0x1143000,0x1144000
        m.put(ctx,actor);m.put(actor+0x3d4,30. if fast else 10.,'f')
        m.put(entry+0x34,target);m.put(target+0x1d4,ref);m.put(ref+0x1c,body)
        m.put(body+0x90,10.,'f');m.put(body+0xe4,0)
        def setdesc(at,d):
            m.put(at,d[0]);m.put(at+4,d[1]);m.put(at+8,d[2],'f');m.put(at+12,int(d[3]),'B')
        def getdesc(at):return [m.get(at),m.get(at+4),m.get(at+8,'f'),bool(m.get(at+12,'B'))]
        setdesc(desc,current);calls=[]
        def evaluation(x):
            sp=x.reg('esp');d=getdesc(x.get(sp+4));admitted=bool(x.get(sp+8))
            score=20. if d[0] else -1.;gap=8. if d[0]==accepted else .2
            calls.append(dict(descriptor=d,side=admitted,score=score,gap=gap))
            x.put(x.get(sp+12),score,'f');x.put(x.get(sp+16),target);x.put(x.get(sp+20),gap,'f')
            x.put(x.reg('edi'),1,'B');x.put(x.reg('edi')+16,float(len(calls)),'f')
            x.ret(0x1c,value=int(score> -1 and gap>2.))
        def fallback(x):
            out=x.get(x.reg('esp')+8);x.put(out,1,'B');x.put(out+16,-99.,'f');x.ret(0x14)
        m.hook(0x45c0d0,'declared-side-policy',pop=8,value=policy)
        m.hook(0x45ad30,'declared-category-admission',value=1)
        m.hook(0x45b9f0,'declared-side-room',value=int(room))
        m.hook(0x45b050,'declared-geometry-fitness-collision',callback=evaluation)
        m.hook(0x45ae10,'declared-fallback-path',callback=fallback)
        m.hook(0x45bfb0,'publication-outside-selection',pop=12)
        m.run(0x45bae0,(desc,path,side,entry,ctx,0,bits(2.),1),budget=300000)
        result=dict(descriptor=getdesc(desc),side=bool(m.get(side,'B')),found=bool(m.reg('eax')&255),marker=m.get(path+16,'f'))
        def lit(v):
            if isinstance(v,bool):return str(v).lower()
            if isinstance(v,float):return literal(v)
            if isinstance(v,(list,tuple)):return '{'+','.join(map(lit,v))+'}'
            return str(v)+'u' if isinstance(v,int) and v>0xffff else str(v)
        rows=[[r['descriptor'][0],r['descriptor'][1],r['descriptor'][3],r['score'],r['gap'],target] for r in calls]
        lines.append(lit([current,fast,policy,room,accepted,result['descriptor'],result['side'],result['found'],result['marker'],len(calls),rows])+',')
        evidence.append(dict(input=[current,fast,policy,room,accepted],result=result,calls=calls,hooks=m.events))
    a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(sha256=sha,scope=__doc__,cases=evidence),indent=2)+'\n',encoding='ascii')
    print('Original target selection:',len(cases),'cases; every service hook recorded')
if __name__=='__main__':main()
