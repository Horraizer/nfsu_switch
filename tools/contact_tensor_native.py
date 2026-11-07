"""Original584B10/585810 contact tensor and refresh on explicit rigid inputs.

No contact geometry, suspension, scheduler or whole-race claim. The sampled
matrix is intentionally not normalized:469390 retains that interpolation.
"""
from pathlib import Path
import sys,json,hashlib,math
from world_body_trace import Image
import contact256_native as n

if len(sys.argv)!=3:raise SystemExit('usage: contact_tensor_native.py retail-root fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
for alternate in (False,True):
 for sampled in (False,True):
  for momentum in ((30.,4.,2.),(300.,400.,0.)):
   m=n.fixture(im,(4,0,0),(0,0,0),(0,0,0),mass=1.22)
   rigid=n.RIGID_A;properties=0x112f000
   m.put(rigid+0x14,properties);m.put(properties,1.22,'f')
   n.matrix(m,properties+0x10)
   for offset,value in ((0,.45955365896224976),(0x14,2.246273994445801),(0x28,2.367461681365967)):
    m.put(properties+0x10+offset,value,'f')
   m.put(properties+0x50,.4,'f');m.run(0x584b10,esi=rigid)
   c,s=math.cos(.7),math.sin(.7)
   axes=[[c,s,0],[-s,c,0],[0,0,1]]
   if sampled:
    axes=[[.5*(v+(1 if i==k else 0)) for k,v in enumerate(axis)] for i,axis in enumerate(axes)]
   for k,axis in enumerate(axes):n.vector(m,rigid+0x30+16*k,axis)
   m.put(rigid+0x138,int(alternate));n.vector(m,rigid+0x70,momentum);n.vector(m,rigid+0x80,(2.,-3.,4.))
   row=dict(alternate=alternate,axes=[n.vector(m,rigid+0x30+16*k) for k in range(3)],
    mass=m.get(properties,'f'),inertia=[m.get(properties+0x10+o,'f') for o in (0,0x14,0x28)],
    scale=m.get(properties+0x50,'f'),momentum=n.vector(m,rigid+0x70),angular=n.vector(m,rigid+0x80))
   def tensor(x):
    at=x.get(x.reg('esp')+12)
    row['tensor']=[[x.get(at+16*k+4*j,'f') for j in range(3)] for k in range(3)]
   m.observers[0x5abdaf]=tensor
   m.run(0x585810,ecx=rigid,budget=3000000)
   row.update(after_momentum=n.vector(m,rigid+0x70),velocity=n.vector(m,rigid+0xa0),omega=n.vector(m,rigid+0xc0))
   rows.append(row)
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(dict(executable_sha256=sha,cases=rows),indent=2),encoding='ascii')
def f(x):return float(x).hex()+'F'
lines=[]
for r in rows:
 values=[r['alternate'],r['mass'],*r['inertia'],r['scale']]+sum(r['axes'],[])+r['momentum']+r['angular']+sum(r['tensor'],[])+r['after_momentum']+r['velocity']+r['omega']
 lines.append('{'+','.join(f(x) for x in values)+'},')
(out/'pins.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
print('PASS: eight original rigid tensor/refresh cases; explicit physical inputs')
