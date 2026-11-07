"""Original static polygon shape/query/contact normal on controlled planes.
Car geometry is authenticated retail240SX. Polygon corners are explicit inputs.
"""
from pathlib import Path
import sys,json,hashlib
from world_body_trace import Image,jdlz,chunks
import contact256_native as n
from contact257_retail import inputs,prepare
from physics256_native import bits
if len(sys.argv)!=3:raise SystemExit('usage: track_contact_native.py retail-root fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for name,normal,corners,positions in (
 ('floor',(0,0,1),((-8,-8,0),(8,-8,0),(8,8,0),(-8,8,0)),((0,0,.5),(0,0,.1),(0,0,-.5))),
 ('front-wall',(-1,0,0),((2,-8,-8),(2,-8,8),(2,8,8),(2,8,-8)),((0,0,0),(-.5,0,0),(1,0,0))),
 ('side-wall',(0,-1,0),((-8,1,-8),(8,1,-8),(8,1,8),(-8,1,8)),((0,0,0),(0,.5,0),(0,2,0)))):
 for position in positions:
  m=n.fixture(im,(0,0,0),(10,0,0),(0,0,0));prepare(m,n,data)
  polygon,origin=0x112c000,0x112d000
  for k,c in enumerate(corners):n.vector(m,polygon+0x30+k*16,c)
  n.vector(m,polygon+0x70,normal);n.vector(m,origin,(0,0,0))
  m.run(0x473410,esi=n.SHAPE_B)
  m.run(0x473480,(polygon,bits(1.)),ecx=n.SHAPE_B,eax=origin)
  m.put(n.BODY_B,0x6c8630);m.put(n.BODY_B+0x15,3,'B');m.put(n.BODY_B+0x16,0,'B')
  m.put(n.BODY_B+0x60,polygon);m.put(n.BODY_B+0x10,n.SHAPE_B)
  n.vector(m,n.RIGID_A+0x20,position);n.vector(m,n.ACTOR_A+0x60,position)
  n.vector(m,n.BODY_A+0x110,position)
  m.run(0x468a50,edi=n.BODY_A)
  bounds=dict(below=m.get(n.BODY_A+0xe0,'f'),above=m.get(n.BODY_A+0xe4,'f'),
   radius=m.get(n.BODY_A+0xe8,'f'),centre=n.vector(m,n.BODY_A+0x120))
  m.put(polygon+0x7c,sum(corners[0][k]*normal[k] for k in range(3)),'f')
  m.run(0x469a00,edi=n.BODY_A,esi=polygon)
  plane_admitted=m.reg('eax')!=0
  m.run(0x4727c0,ecx=n.PAIR,eax=0x29f0000,edx=n.SHAPE_B)
  m.put(n.SHAPE_B+0x1d0,0,'B');q=n.query(m)
  m.run(0x46b8c0,edi=n.JOB,eax=n.NORMAL,ecx=n.POINT)
  geometry=dict(point=n.vector(m,n.POINT),normal=n.vector(m,n.NORMAL))
  m.put(n.SHAPE_B+0x1d0,1,'B');m.put(n.PAIR+0x240,1,'B');expanded=n.query(m)
  m.put(n.SHAPE_B+0x1d0,0,'B');m.put(n.PAIR+0x240,1,'B');n.query(m)
  m.put(0x736200,0);physics=0x1130000
  m.put(n.ACTOR_A+0x34,physics);m.put(physics,0x6c8468)
  start=tuple(position[k]+normal[k]*.5 for k in range(3))
  m.put(n.BODY_A+0x70,.016,'f');n.vector(m,n.BODY_A+0xc0,start)
  n.matrix(m,n.BODY_A+0x80);n.vector(m,n.BODY_A+0xd0,(0,0,0))
  trace=[];remaining=[]
  def searched(x):
   trace.append(dict(remaining=x.get(x.reg('esp')+0x14,'f'),distance=x.get(n.PAIR+0x10,'f'),
    deep=x.get(n.PAIR+0x15,'B'),contact=x.get(n.PAIR+0x14,'B')))
  m.observers[0x46afda]=searched
  m.observers[0x46b020]=lambda x:remaining.append(x.get(x.reg('esp')+0x14,'f'))
  m.run(0x46ae60,eax=n.JOB,budget=3000000)
  search=dict(start=start,trace=trace,remaining=remaining[-1],correction=n.vector(m,n.JOB+0x20),
   normal_side=m.get(n.PAIR+0x274),distance=m.get(n.PAIR+0x10,'f'),
   a=n.vector(m,n.PAIR+0x20),b=n.vector(m,n.PAIR+0x30))
  rows.append(dict(name=name,position=position,corners=corners,normal=normal,query=q,geometry=geometry,expanded=expanded,search=search,
   bounds=bounds,plane_admitted=plane_admitted))
  print(name,position,q['scalar'],geometry,expanded['scalar'],flush=True)
track=(root/'Tracks/TRACKB1003.lzc').read_bytes()
assert hashlib.sha256(track).hexdigest()=='ada8206b5d66e3286bf582e2c7117fe0cc2ba9f6cadb756d922f7d32e7c696ba'
blob=jdlz(track);tag,start,end=next(c for c in chunks(blob) if c[0]==0x34132)
decoded=[]
for index in (0,8919,8920,9950):
 record=blob[start+8+32*index:start+8+32*(index+1)]
 m=n.fixture(im,(0,0,0),(0,0,0),(0,0,0));raw_at,obj,plain=0x1130000,0x1140000,0x1150000
 m.uc.mem_write(raw_at,record);m.run(0x43fd20,eax=raw_at,ecx=plain)
 raw_corners=[n.vector(m,plain+16*k) for k in range(4)]
 m.run(0x43ff30,end=0x44001d,edi=raw_at,esi=obj)
 corners=[n.vector(m,obj+0x30+16*k) for k in range(4)]
 expected=[[p[0],p[1],p[2]*(4 if record[3]&4 else 1)] for p in raw_corners]
 assert corners==expected
 decoded.append(dict(index=index,flags=record[3],surface=record[2],raw=raw_corners,corners=corners))
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(dict(executable_sha256=sha,cases=rows,decoded_polygons=decoded),indent=2),encoding='ascii')
print('PASS: four original polygon decode/flag cases')
