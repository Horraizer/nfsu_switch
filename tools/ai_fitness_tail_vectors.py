"""Original459B30 tail boundary; poison unused stack to check result dependence."""
import argparse, hashlib, json, struct
from pathlib import Path
from world_body_trace import Image
from physics256_native import Machine, ACTOR, STACK
from ai_path_vectors import literal

p=argparse.ArgumentParser()
for key in ('root','snapshot','output','evidence'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
trace=a.snapshot.read_text(encoding='utf-8');row=next(s for s in trace.splitlines() if s.startswith('FITNESS_INPUT ')).split()
points=[list(map(float,s.split()[1:])) for s in trace.splitlines() if s.startswith('FITNESS_POINT ')]
cases=[dict(name='retail-speed-frame280',signed=float(row[1]),absolute=float(row[2]),position=list(map(float,row[4:7])),velocity=list(map(float,row[7:10])),points=points)]
for n in (2,3,7):
    cases.append(dict(name='short-tail-'+str(n),signed=40.,absolute=40.,position=[0.,0.,0.],velocity=[40.,0.,0.],points=[[.1*j,0.,0.] for j in range(1,n+1)]))
lines=['// Original459B30 short tail; tools/ai_fitness_tail_vectors.py. '+sha]
receipts=[]
for c in cases:
    results=[]
    for poison in (0,0x3f800000,0x7fc01234,0x7f800000,0xff800000):
        m=Machine(Image(raw));m.allowed=[(0x459b30,0x459ff8),(0x45a000,0x45a1dc),(0x459710,0x459867),(0x5672b0,0x567365),(0x41f5e0,0x41f5e4)]
        path,store,out=0x1150000,0x1151000,0x1151100
        m.uc.mem_write(STACK-4096,struct.pack('<I',poison)*1024)
        m.uc.mem_write(path,struct.pack('<I',poison)*(0x1c0//4))
        m.put(path,len(c['points']),'B');m.put(path+0x1b0,255,'B');m.put(path+0x1a4,0)
        m.put(0x73ad34,22666);m.put(0x77a4a8,16.,'f')
        for j,point in enumerate(c['points']):
            for k,v in enumerate(point):m.put(path+16+16*j+4*k,v,'f')
        for at,v in ((ACTOR+0x60,c['position']),(ACTOR+0x70,c['velocity'])):
            for k,x in enumerate(v):m.put(at+4*k,x,'f')
        m.put(ACTOR+0x3d0,c['signed'],'f');m.put(ACTOR+0x3d4,c['absolute'],'f')
        m.run(0x459b30,(path,ACTOR));visited=sorted(m.visited)
        code=b'\xd9\x1d'+struct.pack('<I',out)+b'\xc3';m.uc.mem_write(store,code);m.allowed.append((store,store+7));m.run(store)
        assert not m.events
        results.append(dict(poison=poison,score_bits=m.get(out),cursor=m.get(path+0x1a0,'B'),visited=visited))
    assert len({r['score_bits'] for r in results})==1
    receipts.append(dict(input=c,results=results))
    # Separate absolute and signed speed are needed by the captured live case.
    lines.append('{%s,%s,{%s},{%s},%du,0x%08Xu,{{%s}}},'%(
        literal(c['signed']),literal(c['absolute']),','.join(map(literal,c['position'])),','.join(map(literal,c['velocity'])),
        len(c['points']),results[0]['score_bits'],','.join('{{'+','.join(map(literal,pt))+'}}' for pt in c['points'])))
a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
a.evidence.write_text(json.dumps(dict(sha256=sha,cases=receipts,scope=__doc__),indent=2)+'\n',encoding='ascii')
print('PASS: 4 tail cases x 5 stack/path poison patterns, original execution without hooks; identical score bits')
