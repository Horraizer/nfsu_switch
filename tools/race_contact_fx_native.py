"""Execute465830/598260 FX admission with explicit material/near-view inputs.

Collision-state mutation and audio distance are bounded out.402D30 submission
is observed, not replaced by a claim about original GPU output or geometry.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from hud254_trace import PE
from physics256_native import Machine,bits
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bank=(args.retail_root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(bank).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
stock=[bank[0x2034a0+544*i:0x2034a0+544*(i+1)] for i in range(90)]
wall=[r for r in stock if r[0x1f0:].split(b'\0',1)[0]==b'XECS_SOLID_WALL'];assert len(wall)==2
rows=[]
for surface,impulse,slide in ((1,15,0),(1,100,100),(20,15,0),(20,0,50),(20,1,1)):
 m=Machine(PE(raw));m.allowed=[(0x465830,0x465b09),(0x598260,0x598354),(0x401cd0,0x401d60),(0x40a880,0x40a881),(0x567370,0x567400)]
 record,point,force,table,pair,obj,vtable,velocity=tuple(0x1100000+4096*i for i in range(8))
 m.put(point,1.,'f');m.put(point+4,2.,'f');m.put(point+8,3.,'f');m.put(force+8,impulse,'f')
 m.put(0x734460,table);m.put(record+0x64,pair)
 for at in (0x258,0x25c):m.put(pair+at,obj)
 m.put(obj,vtable);m.put(vtable+0xc,0x1000600);m.hook(0x1000600,'supplied-carrier-velocity',value=velocity)
 for kind in range(7):
  for mat in range(29):
   head=table+8*(29*kind+mat);m.put(head,head);m.put(head+4,head)
   address=m.get(0x6ef2d0+148*mat+0x7c)
   name=bytes(m.uc.mem_read(address,40)).split(b'\0',1)[0]
   if kind not in (5,6) or mat!=20:continue
   assert name==b'XECS_SOLID_WALL'
   nodes=[0x1110000+kind*0x1000+i*0x100 for i in range(2)];m.put(head,nodes[0])
   for i,node in enumerate(nodes):
    m.put(node,nodes[i+1] if i+1<len(nodes) else head);preset=0x11b0000+kind*0x1000+i*0x300
    m.uc.mem_write(preset,wall[i]);m.put(node+0x1c,preset)
 m.hook(0x465d00,'omitted-collision-state-mutation',pop=12)
 m.scalar_hook(0x4212f0,'supplied-near-view-distance')
 m.scalar_hook(0x421400,'supplied-audio-distance')
 m.hook(0x466350,'supplied-selected-material',value=surface)
 m.put(0x6f1dd8,0)
 calls=[]
 def submit(x):
  sp=x.reg('esp');source=x.get(sp+8);preset=x.get(source+0x1c)
  calls.append(dict(preset=bytes(x.uc.mem_read(preset+0x180,80)).split(b'\0',1)[0].decode('ascii'),strength=-x.get(sp+12,'f'),position=[x.get(source+0x60+4*k,'f') for k in range(3)]))
  x.ret(16)
 m.hook(0x402d30,'observed-original-particle-submit',callback=submit)
 m.run(0x465830,(record,point,force,bits(slide)),end=0x465b09)
 rows.append(dict(surface=surface,impulse=impulse,slide=slide,calls=calls,hooks=m.events));print(rows[-1])
assert not rows[0]['calls'] and not rows[1]['calls'] and not rows[4]['calls']
assert len(rows[2]['calls'])==len(rows[3]['calls'])==2
assert all(c['position']==[1.,2.,3.] for r in rows for c in r['calls'])
(args.out/'contact-fx-native.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
assert (args.retail_root/'speed.exe').read_bytes()==raw
print('PASS direct ROAD body contact has no FX; strong wall impact/scrape dispatch both original wall records')
