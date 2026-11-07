"""Read-only original459B30 geometry fitness on explicit paths/current bodies.
No result/service hooks. Does not simulate strategy, contacts or a full race.
"""
import argparse,hashlib,json,math,struct
from pathlib import Path
from physics256_native import Machine,ACTOR,bits
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence','paths'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(raw);source=json.loads(a.paths.read_text())
    if source['executable_sha256']!=sha:raise ValueError('path provenance mismatch')
    m=Machine(image);m.allowed=[(0x459b30,0x459f70),(0x459f70,0x459ff8),(0x45a000,0x45a1dc),
        (0x459710,0x459867),(0x5672b0,0x567304),(0x567310,0x567365),(0x41f5e0,0x41f5e4)]
    path=0x1150000;store=0x1151000;out=0x1151100
    code=b'\xd9\x1d'+struct.pack('<I',out)+b'\xc3';m.uc.mem_write(store,code);m.allowed.append((store,store+len(code)))
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    cases=[]
    def add(name,points,speed=20.,position=(0.,0.,0.),velocity=None,end=255,limit=None,cached=None):
        cases.append(dict(name=name,points=points,speed=speed,position=position,
            velocity=velocity if velocity is not None else (speed,0.,0.),end=end,limit=limit,cached=cached))
    add('empty',[]);add('single',[(10.,2.,0.)]);add('single-zero-distance',[(0.,0.,0.)])
    for speed in (0.,5.,20.,40.,70.,-5.):
        for bend in (0.,.01,.04,.12):
            points=[(15.*(j+1),f32(bend*(15.*j)**2),0.) for j in range(12)]
            add('bend-%s-%s'%(speed,bend),points,speed)
        add('right-angle-'+str(speed),[(15.,0.,0.),(30.,0.,0.),(30.,30.,0.),(30.,60.,0.)],speed)
    # Adjacent lateral-acceleration limits straddle one actual first-turn bound.
    for limit in (31.99,32.,32.01):add('limit-'+str(limit),[(30.,40.,0.),(60.,80.,0.),(90.,120.,0.)],20.,limit=limit)
    add('cached-cursor',[(20.,0.,0.),(40.,5.,0.),(60.,10.,0.),(80.,10.,0.)],cached=1)
    add('transition-prefix',[(20.,0.,0.),(40.,0.,0.),(40.,20.,0.),(20.,20.,0.)],end=1)
    for c in source['cases']:
        actor=c['actor'];points=c['expected']['points']
        add('retail-'+c['name'],points,20.,actor[:3],actor[3:6])
    rows=[];lines=['// Original459B30, speed.exe SHA256 '+sha,'// Explicit bodies/paths; 007F x87, no hooks.']
    for number,c in enumerate(cases):
        m.uc.mem_write(path,bytes(0x1c0));m.put(path,len(c['points']),'B');m.put(path+0x1b0,c['end'],'B')
        tick=100000+number;m.put(0x73ad34,tick);m.put(0x77a4a8,16.,'f')
        for j,p in enumerate(c['points']):
            for k,v in enumerate(p):m.put(path+16+16*j+4*k,v,'f')
        for at,values in ((ACTOR+0x60,c['position']),(ACTOR+0x70,c['velocity'])):
            for k,v in enumerate(values):m.put(at+4*k,v,'f')
        m.put(ACTOR+0x3d0,c['speed'],'f');m.put(ACTOR+0x3d4,abs(c['speed']),'f')
        m.put(0x6b7530,30. if c['limit'] is None else c['limit'],'f')
        if c['cached'] is not None:m.put(path+0x1a0,c['cached'],'B');m.put(path+0x1a4,tick)
        m.visited.clear();m.run(0x459b30,(path,ACTOR));visited=sorted(m.visited);m.run(store)
        expected=m.get(out);cursor=m.get(path+0x1a0,'B');cache=m.get(path+0x1a4)
        params=[m.get(v,'f') for v in (0x77a4a8,0x6b7520,0x6b7524,0x6b7528,0x6b752c,0x6b7530)]
        lines.append('// '+c['name'])
        lines.append('{%d,{%s},{%s},%s,%d,{%s},%d,%d,%d,0x%08Xu,%d,%d,{'%(len(c['points']),
            ','.join(map(literal,c['position'])),','.join(map(literal,c['velocity'])),literal(c['speed']),tick,
            ','.join(map(literal,params)),c['end'],c['cached'] or 0,tick if c['cached'] is not None else 0,expected,cursor,cache))
        for point in c['points']:lines.append('    {%s},'%','.join(map(literal,point)))
        lines.append('}},')
        rows.append(dict(input=c,parameters=params,score_bits=expected,score=m.get(out,'f'),cursor=cursor,cursor_tick=cache,visited=visited))
    a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=sha,input_paths_sha256=hashlib.sha256(a.paths.read_bytes()).hexdigest(),
        scope=__doc__,fpu='007F',hooks=m.events,cases=rows),indent=2)+'\n',encoding='ascii')
    print('Original fitness:',len(rows),'cases;',sum(r['score']>=0 for r in rows),'admitted;',sum(r['score']<0 for r in rows),'rejected; hooks',len(m.events))
if __name__=='__main__':main()
