"""Original46BAE0 kind0/kind3 response on explicit prepared witnesses.

This isolates response from distance/search and world admission. It is not a
live wall hit. Original instructions execute; only the effects tail is omitted.
"""
from pathlib import Path
import sys,json,hashlib,math
from world_body_trace import Image
import contact256_native as n
from contact257_retail import inputs,prepare
from physics256_native import bits
from unicorn.x86_const import UC_X86_REG_EIP
if len(sys.argv)!=3:raise SystemExit('usage: static_response_native.py retail-root fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for name,velocity,point,roll,state in (
 ('head',(10,0,0),(1,0,0),0,0),('slow',(.01,0,0),(1,0,0),0,0),
 ('fast',(40,3,0),(1,.2,.3),0,0),('side',(10,3,0),(.1,1,.3),0,0),
 ('tilted',(40,3,0),(1,.2,.3),1.2,0),('crash',(40,3,0),(1,.2,.3),0,1),
 ('separating',(-10,3,0),(1,.2,.3),0,0)):
 m=n.fixture(im,(0,0,0),velocity,(0,0,0),mass=2,state=state)
 prepare(m,n,data)
 polygon,origin=0x112c000,0x112d000
 for k,c in enumerate(((1,-8,-8),(1,-8,8),(1,8,8),(1,8,-8))):n.vector(m,polygon+0x30+k*16,c)
 n.vector(m,polygon+0x70,(-1,0,0));n.vector(m,origin,(0,0,0))
 m.run(0x473410,esi=n.SHAPE_B);m.run(0x473480,(polygon,bits(1)),ecx=n.SHAPE_B,eax=origin)
 m.put(n.BODY_B,0x6c8630);m.put(n.BODY_B+0x15,3,'B');m.put(n.BODY_B+0x16,0,'B')
 m.put(n.BODY_B+0x60,polygon);m.put(n.BODY_B+0x10,n.SHAPE_B)
 c,s=math.cos(roll),math.sin(roll)
 axes=((1,0,0),(0,c,s),(0,-s,c))
 for k,v in enumerate(axes):n.vector(m,n.RIGID_A+0x30+16*k,v);n.vector(m,n.ACTOR_A+0x90+16*k,v)
 n.vector(m,n.PAIR+0x20,point);n.vector(m,n.PAIR+0x30,point)
 m.run(0x46b8c0,edi=n.JOB,eax=n.NORMAL,ecx=n.POINT)
 applied=[]
 def apply(x):
  sp=x.reg('esp');applied.append(dict(impulse=n.vector(x,x.get(sp+4)),arm=n.vector(x,x.get(sp+8))))
 m.observers[0x46a260]=apply
 m.hook(0x4658b5,'declared_effect_tail_skip',callback=lambda x:x.uc.reg_write(UC_X86_REG_EIP,0x465cf2))
 m.run(0x46bae0,(n.JOB,),budget=3000000)
 rows.append(dict(name=name,velocity=velocity,point=point,state=state,
  axes=[n.vector(m,n.RIGID_A+0x30+16*k) for k in range(3)],normal=n.vector(m,n.NORMAL),
  applied=applied,momentum=n.vector(m,n.RIGID_A+0x70),angular=n.vector(m,n.RIGID_A+0x80)))
 print(name,applied,flush=True)
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(dict(executable_sha256=sha,cases=rows),indent=2),encoding='ascii')
