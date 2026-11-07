"""Authenticated original46AE60/468FE0 car-pair fixtures.

Explicit synthetic history and selector0; retail hulls/handling are authenticated.
No solver, controller or full-game runtime claim. No original instructions patched.
"""
from pathlib import Path
import sys,json,hashlib,math,struct
from world_body_trace import Image
import contact256_native as n
from contact257_retail import prepare,inputs
if len(sys.argv)!=3:raise SystemExit('usage: contact_search_native.py retail-root output-json')
output=Path(sys.argv[2]).resolve();root=Path(sys.argv[1]).resolve()
if output==root or root in output.parents:raise ValueError('retail output forbidden')
if output.exists():raise ValueError('output exists; select a fresh evidence path')
root=Path(sys.argv[1])
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for retail in (False,True):
 for name,start,end,ta,tb in [('closing',5.,4.,.016,.016),('deep_start',4.,3.9,.016,.016),('unequal',5.,4.,.016,.008),('zero',5.,4.,0.,0.)]:
  if not retail:start-=2.4;end-=2.4
  m=n.fixture(im,(end,0,0),(30,0,0),(0,0,0))
  if retail:prepare(m,n,data)
  m.put(0x736200,0)
  for actor,physics in ((n.ACTOR_A,0x1130000),(n.ACTOR_B,0x1131000)):
   m.put(actor+0x34,physics);m.put(physics,0x6c8468)
  for body,old,duration in ((n.BODY_A,(0,0,0),ta),(n.BODY_B,(start,0,0),tb)):
   m.put(body+0x70,duration,'f');n.vector(m,body+0xc0,old);n.matrix(m,body+0x80);n.vector(m,body+0xd0,(0,0,0))
  endpoint=n.query(m);trace=[];last=[]
  def after(x):
   trace.append(dict(remaining=x.get(x.reg('esp')+0x14,'f'),scalar=x.get(n.PAIR+0x10,'f'),deep=x.get(n.PAIR+0x15,'B'),contact=x.get(n.PAIR+0x14,'B'),a=n.vector(x,n.PAIR+0x40),b=n.vector(x,n.PAIR+0x50)))
  m.observers[0x46afda]=after
  m.observers[0x46b020]=lambda x:last.append(x.get(x.reg('esp')+0x14,'f'))
  m.run(0x46ae60,eax=n.JOB,budget=3000000)
  row=dict(name=name,retail=retail,start=start,end=end,ta=ta,tb=tb,endpoint=endpoint,trace=trace,last=last,shift=n.vector(m,n.JOB+0x20),final=dict(scalar=m.get(n.PAIR+0x10,'f'),a=n.vector(m,n.PAIR+0x20),b=n.vector(m,n.PAIR+0x30)))
  rows.append(row);print(json.dumps(row))
samples=[]
for duration,remaining in ((.016,.008),(.016,.004),(.008,.006),(0.,0.),(.0000005,.00000025)):
 m=n.fixture(im,(0,0,0),(11.,-3.,2.),(0,0,0))
 m.put(n.BODY_A+0x70,duration,'f')
 n.vector(m,n.BODY_A+0xc0,(1.,2.,3.));n.vector(m,n.RIGID_A+0x20,(2.,4.,5.))
 n.vector(m,n.BODY_A+0xd0,(3.,-1.,7.))
 n.matrix(m,n.BODY_A+0x80)
 # Exact quarter turn makes matrix interpolation distinguishable from nlerp.
 for i,v in enumerate((0.,1.,0.,0.,-1.,0.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)):
  m.put(n.RIGID_A+0x30+4*i,v,'f')
 pos,axes,velocity=0x1140000,0x1140100,0x1140200
 m.run(0x468fe0,(pos,axes,n.bits(remaining)),ecx=n.BODY_A)
 m.run(0x4692f0,(n.bits(remaining),),esi=n.BODY_A,edi=velocity)
 samples.append(dict(duration=duration,remaining=remaining,position=n.vector(m,pos),
  axes=[[m.get(axes+16*i+4*k,'f') for k in range(3)] for i in range(3)],velocity=n.vector(m,velocity)))
output.write_text(json.dumps(dict(search=rows,sample=samples),indent=2),encoding='ascii')
print('PASS: eight time-search and five matrix/velocity boundaries; synthetic histories')
