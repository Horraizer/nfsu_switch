"""Original45A5B0 path prediction on saved authored paths and explicit bodies.
No vehicle motion, service replacement or collision-result hooks.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from physics256_native import Machine,bits
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence','paths'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    im=Image(raw);m=Machine(im);m.allowed=[(0x45a5b0,0x45a7ce),(0x45a8c0,0x45a918)]
    source=json.loads(a.paths.read_text());assert source['executable_sha256']==sha
    path,position,velocity,acceleration=0x1150000,0x1151000,0x1151100,0x1151200
    rows=[];lines=['// Original45A5B0, 007F x87, no hooks. SHA256 '+sha]
    for c in source['cases']:
      points=c['expected']['points'];actor=c['actor']
      for name,speed_scale,accel,first,cache,horizon in [('coast',1.,(0.,0.,0.),0,False,None),
          ('turn',1.,(4.,-3.,0.),0,False,None),('brake',1.,(6.,12.,0.),0,False,None),
          ('retained',1.,(3.,4.,0.),3,False,None),('stationary',0.,(3.,4.,0.),0,False,None),
          ('slow',.04,(3.,4.,0.),0,False,None),('disabled',1.,(3.,4.,0.),0,False,0.),
          ('same-tick',1.,(8.,9.,0.),0,True,None),('short',1.,(4.,-3.,0.),0,False,.25)]:
        m.uc.mem_write(path,bytes(0x1c0));m.put(path,len(points),'B');tick=4000+len(rows)
        for j,p in enumerate(points):
            for k,v in enumerate(p):m.put(path+16+16*j+4*k,v,'f')
            m.put(path+0x150+j,j,'B');m.put(path+0x164+2*j,j+100,'H');m.put(path+0x18c+j,j%5,'B')
        m.put(path+0x1a0,first,'B');m.put(path+0x1a4,3000);m.put(path+0x1a8,tick if cache else 0);m.put(0x73ad34,tick)
        speed=[v*speed_scale for v in actor[3:6]]
        for at,values in ((position,actor[:3]),(velocity,speed),(acceleration,accel)):
            for k,v in enumerate(values):m.put(at+4*k,v,'f')
        duration=struct.unpack('<f',im.read(0x6b7538,4))[0] if horizon is None else horizon
        m.put(0x6b7538,duration,'f');m.run(0x45a8c0,ecx=path);initial_length=m.get(path+0x1ac)
        m.visited.clear();m.run(0x45a5b0,(position,acceleration),eax=velocity,ecx=path,edx=first)
        expected=[[m.get(path+16+16*j+4*k) for k in range(3)] for j in range(len(points))]
        after=[m.get(path+0x1ac),m.get(path+0x1a4),m.get(path+0x1a8)]
        identity=[[m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H'),m.get(path+0x18c+j,'B')] for j in range(len(points))]
        assert identity==[[j,j+100,j%5] for j in range(len(points))]
        lines.append('// '+c['name']+'-'+name)
        lines.append('{%d,{%s},{%s},{%s},%s,%d,%d,%d,0x%08Xu,{%s},{'%(len(points),
            ','.join(map(literal,actor[:3])),','.join(map(literal,speed)),','.join(map(literal,accel)),
            literal(duration),first,tick,tick if cache else 0,initial_length,','.join('0x%08Xu'%v for v in after)))
        for pt in points:lines.append('    {%s},'%','.join(map(literal,pt)))
        lines.append('},{')
        for pt in expected:lines.append('    {%s},'%','.join('0x%08Xu'%v for v in pt))
        lines.append('}},')
        rows.append(dict(name=c['name']+'-'+name,points=points,position=actor[:3],velocity=speed,acceleration=accel,
            horizon=duration,first=first,tick=tick,cached=cache,initial_length=initial_length,
            expected=expected,after=after,visited=sorted(m.visited)))
    a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(scope=__doc__,executable_sha256=sha,
        input_paths_sha256=hashlib.sha256(a.paths.read_bytes()).hexdigest(),fpu='007F',hooks=m.events,cases=rows),indent=2)+'\n',encoding='ascii')
    print('Original path prediction:',len(rows),'cases; hooks',len(m.events))
if __name__=='__main__':main()
