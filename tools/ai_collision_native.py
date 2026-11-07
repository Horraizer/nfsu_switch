"""Execute original474A90 policy and474DD0 complete stabilization.
Synthetic actor/body inputs, authenticated executable; no game runtime claim.
"""
from pathlib import Path
import sys,json,hashlib,struct,math
from world_body_trace import Image
import contact256_native as n
if len(sys.argv)!=3:raise SystemExit('usage: ai_collision_native.py retail-root fresh-output-directory')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
# Flat, yawed, pitched, rolled actor frames; support/state boundaries and a
# stopped airborne control. Every return must reach the original epilogue.
for state,support,angle,kind,velocity in [(s,w,0.,'yaw',(20.,3.,1.)) for s in (0,1,2) for w in (0,1,4)]+[(0,0,a,k,(20.,3.,1.)) for k in ('yaw','pitch','roll') for a in (.3,1.2)]+[(0,0,0.,'yaw',(0.,0.,1.)),(0,0,0.,'yaw',(0.,20.,0.))]:
 m=n.fixture(im,(4,0,0),(0,0,0),(0,0,0),mass=1.22,state=state)
 physics,aero,engine=0x1127000,0x1128000,0x1129000
 for off,value in ((0x34,n.ACTOR_A),(0x20,n.RIGID_A),(0x24,engine),(0x28,aero)):m.put(physics+off,value)
 m.put(n.ACTOR_A+0x3cc,support);m.put(n.RIGID_A+0x144,1.22,'f')
 c,s=math.cos(angle),math.sin(angle)
 axes=((c,s,0),(-s,c,0),(0,0,1)) if kind=='yaw' else ((c,0,s),(0,1,0),(-s,0,c)) if kind=='pitch' else ((1,0,0),(0,c,s),(0,-s,c))
 for k,axis in enumerate(axes):n.vector(m,n.ACTOR_A+0x90+16*k,axis)
 n.vector(m,n.ACTOR_A+0x70,velocity)
 omega=(1.,-2.,3.);torque=(.5,-.25,.125)
 n.vector(m,n.RIGID_A+0xc0,omega);n.vector(m,n.RIGID_A+0x120,torque)
 m.run(0x474aa9,end=0x474b06,edi=physics)
 flags=[m.get(n.RIGID_A+off) for off in (0x130,0x134,0x138)]+[m.get(aero+0x30,'B')]
 trace=[]
 for site in (0x474f3e,0x474ff8,0x474fcb):
  def observed(x,site=site):trace.append(dict(site=hex(site),scratch=[x.get(x.reg('esp')+z,'f') for z in (0x10,0x14,0x18,0x20,0x24,0x28)]))
  m.observers[site]=observed
 m.run(0x474dd0,eax=physics)
 rows.append(dict(state=state,support=support,axes=[n.vector(m,n.ACTOR_A+0x90+16*k) for k in range(3)],velocity=list(velocity),omega=omega,initial_torque=torque,mass=m.get(n.RIGID_A+0x144,'f'),flags=flags,trace=trace,gravity=m.get(engine+0x190,'f'),torque=n.vector(m,n.RIGID_A+0x120)))
 print('PASS original',len(rows),state,support,kind,angle)
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(dict(executable_sha256=sha,cases=rows),indent=2),encoding='ascii')
def f(x):return float(x).hex()+'F'
lines=[]
for r in rows:
 values=[r['state'],r['support'],r['mass']]+sum(r['axes'],[])+r['velocity']+list(r['omega'])+list(r['initial_torque'])+r['torque']+r['flags']+[r['gravity']]
 lines.append('{'+','.join(f(x) for x in values)+'},')
(out/'pins.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
