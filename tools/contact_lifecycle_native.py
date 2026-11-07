"""Original player collision policy, non-closing records and query cache.
Controlled fixtures; no live world scheduling or complete gameplay claim.
"""
from pathlib import Path
import sys,json,hashlib
from world_body_trace import Image,jdlz
import contact256_native as n
from contact257_retail import inputs
from contact257_episode import episode,TRACK_SHA
if len(sys.argv)!=3:raise SystemExit('usage: contact_lifecycle_native.py retail-root fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);policies=[]
for state in (0,1,2):
 for support in (0,4):
  m=n.fixture(im,(4,0,0),(0,0,0),(0,0,0),state=state)
  physics,aero=0x1127000,0x1128000
  for off,value in ((0x5c,n.ACTOR_A),(0x20,n.RIGID_A),(0x50,aero)):m.put(physics+off,value)
  m.put(n.ACTOR_A+0x3cc,support)
  m.observers[0x4776c6]=lambda x:x.put(x.reg('esp')+0x18,physics)
  m.run(0x4776c6,end=0x477729,ecx=physics,ebx=0)
  flags=[m.get(n.RIGID_A+off) for off in (0x130,0x134,0x138)]+[m.get(aero+0x30,'B')]
  assert flags==[int(state!=1),int(state!=1),int(state!=1 and support!=0),int(state!=1)]
  policies.append(dict(state=state,support=support,flags=flags))
print('PASS original player collision policy6/6')
rest=[]
for speed,support,prior,throttle in ((0.,4,0,0.),(.4,4,0,0.),(1.,4,0,0.),(0.,4,0,.5),(0.,0,0,0.),(0.,0,2,0.)):
 m=n.fixture(im,(4,0,0),(speed,0,0),(0,0,0),mass=1.22)
 driver,timer,status=0x112a000,0x112b000,0x112b004
 m.put(n.ACTOR_A+0x30,driver);m.put(driver+0x1dc,throttle,'f')
 m.put(n.ACTOR_A+0x3d8,2);m.put(n.ACTOR_A+0x3cc,support)
 m.put(n.ACTOR_A+0x3c,n.BODY_A);m.put(status,prior)
 before=n.vector(m,n.RIGID_A+0x70)
 m.run(0x47ac00,(n.ACTOR_A,timer,status),esi=n.RIGID_A,eax=0)
 rest.append(dict(speed=speed,support=support,prior=prior,throttle=throttle,
  state=m.get(status),gravity=m.get(n.RIGID_A+0x140),before=before,
  momentum=n.vector(m,n.RIGID_A+0x70)))
assert [x['state'] for x in rest]==[2,1,0,0,0,2]
print('PASS original selector3 rest6/6')

cache=n.cache_case(im)
# Native wrappers preserve cached witnesses through clean rotation.
assert all(cache['rotated_clean'][k]==cache['resized'][k] for k in ('scalar','flag14','flag15','witness_a','witness_b'))
print('PASS original rotation-only clean cache')
track=(root/'Tracks/TRACKB1003.lzc').read_bytes();assert hashlib.sha256(track).hexdigest().upper()==TRACK_SHA
row=episode(im,n,inputs(root),jdlz(track),(3.,.7,0.),(-10.,0.,0.),(10.,0.,0.))
assert len(row['records'])==1 and row['records'][0]['magnitude_sum']==0 and not row['hits'] and not row['applied']
print('PASS original admitted separating pair owns zero-impulse record')
row={k:v for k,v in row.items() if k not in ('executed','phases')}
cache={k:v for k,v in cache.items() if k not in ('executed','hooks')}
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(dict(executable_sha256=sha,policies=policies,rest=rest,cache=cache,separating=row),indent=2),encoding='ascii')
