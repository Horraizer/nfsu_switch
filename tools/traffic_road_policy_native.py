"""Execute actual458BD0 with traffic6B96B0 virtuals; no replacement decisions.
Synthetic declared event/clock/world state, not a natural traffic session.
"""
import argparse,hashlib,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--write',action='store_true');a=p.parse_args()
raw=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';im=Image(raw)
rows=[]
for direction in (0,1):
 for event_direction in (0,1):
  for event_type in (0,1,2,3,4,5,7,8,9):
   for traffic_type in (0,2,4,5,7):
    for seed in (0,0x3fffffff,0x40000000,0x7fffffff):
     m=Machine(im);m.allowed=[(0x400000,0x6a0000)]
     policy,actor,world,events=0x1100000,0x1101000,0x1103000,0x1104000;event=events+3*32
     m.put(0x736150,events);m.put(0x7361f8,world);m.put(world+0x2d0,seed)
     m.put(policy,0x6b96b0);m.put(policy+4,actor);m.put(policy+0x4d4,direction);m.put(policy+0x478,42)
     m.put(event+0xb,event_type,'B');m.put(event+0xc,traffic_type,'B');m.put(event+0xd,event_direction,'B');m.put(event+0xf,1,'B');m.put(event+8,2,'B')
     for i in range(8):m.put(policy+12+4*i,0x43210000+(i<<10))
     row=[direction,event_direction,event_type,traffic_type,seed]
     for _ in range(2):
      m.run(0x458bd0,(event,),edi=policy)
      row.extend([m.reg('eax')&255,m.get(world+0x2d0)]+[m.get(policy+12+4*i)for i in range(8)]+[m.get(policy+0x478)])
     assert not m.events;rows.append(row)
text='// Actual458BD0/583BB0/549130 and world RNG; supplied event/world state.\nstatic constexpr unsigned kNativeTrafficRoadPolicy[][27]={\n'+''.join(' {'+','.join('0x%08XU'%v for v in row)+'},\n'for row in rows)+'};\n'
assert all(len(row)==27 for row in rows)
if a.write:
 assert not a.output.exists(),'refusing stale fixture';a.output.write_bytes(text.encode('ascii'))
else:assert a.output.read_text('ascii')==text,'native traffic road policy fixture differs'
print('PASS actual traffic policy decisions, cache replays and original RNG; scenarios='+str(len(rows)))
