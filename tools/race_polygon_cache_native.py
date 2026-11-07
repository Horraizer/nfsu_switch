"""Read-only original4407B0 cache captures on retail TRACKB1003."""
from pathlib import Path
import sys,json,hashlib,struct
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image,jdlz
from contact257_retail import inputs
from contact257_episode import episode
import contact256_native as n
if len(sys.argv)!=3:raise SystemExit('usage: race_polygon_cache_native.py RETAIL OUTPUT')
root=Path(sys.argv[1]);out=Path(sys.argv[2])
if out.exists():raise RuntimeError('output already exists')
raw=(root/'speed.exe').read_bytes();track=(root/'Tracks/TRACKB1003.lzc').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(track).hexdigest()=='ada8206b5d66e3286bf582e2c7117fe0cc2ba9f6cadb756d922f7d32e7c696ba'
rows=[]
def cache_setup(m):
 m.run(0x4412b0,(0x77b130,),budget=3000000)
 m.run(0x445720,esi=0x77b180)
 cache=0x1160000;point=0x1161000;velocity=0x1161020
 m.run(0x4406d0,eax=cache)
 # Ordered calls include a retained cache and a rebuild at the same location.
 for p,v in [((1649.18,297.247,56),(0,0,0)),((1649.28,297.247,56),(0,0,0)),((1649.18,297.247,56),(0,60,0)),((1649.18,297.247,56),(0,100,0)),((1300,400,60),(-40,20,0)),((0,0,0),(0,0,0))]:
  n.vector(m,point,p);n.vector(m,velocity,v)
  #42681B..426855 original speed/radius producer; isolate its actor reads.
  actor=n.ACTOR_A;n.vector(m,actor+0x60,p);n.vector(m,actor+0x70,v)
  # The arithmetic is captured by the original instruction slice.
  m.run(0x42681b,end=0x426858,esi=actor)
  radius=m.get(m.reg('esp'),'f')
  # Slice ended with two pushed arguments; run() resets ESP on the next call.
  m.run(0x4407b0,(point,struct.unpack("<I",struct.pack("<f",radius))[0],velocity),edi=cache,budget=3000000)
  count=m.get(cache+0x2a0)
  base=m.get(0x77b140)
  rows.append(dict(position=list(p),velocity=list(v),radius=radius,bounds=[m.get(cache+i,'f') for i in (0,4,8,12,16,20,24,28)],polygons=[(m.get(m.get(cache+0x20+4*i)+0xc)-base)//32 for i in range(count)]))
 #469810 admits at most48 NEW shapes, then sorts by raw polygon address.
 #Use53 overlapping walls in reversed cache order; no pre-existing body.
 shape_rows=[];raw_base=0x1170000;cache2=0x1163000
 m.run(0x4406d0,eax=cache2)
 corners=((0,-8,-8),(0,-8,8),(0,8,8),(0,8,-8))
 for i in range(53):
  raw_polygon=raw_base+32*i
  record=bytes((0,0x80,0,0x10))+struct.pack('<hH',-8,65535)
  record+=struct.pack('<4h',*[int(p[0]*8) for p in corners])
  record+=struct.pack('<4h',*[int(p[1]*8) for p in corners])
  record+=struct.pack('<4h',*[int((p[2]+8)*256) for p in corners])
  m.uc.mem_write(raw_polygon,record)
  m.run(0x441b90,(0x77b130,),eax=raw_polygon,budget=3000000)
  shape_rows.append(m.reg('eax'))
 for i,poly in enumerate(reversed(shape_rows)):m.put(cache2+0x20+4*i,poly)
 m.put(cache2+0x2a0,len(shape_rows));original_cache=m.get(n.BODY_A+0x64)
 m.put(n.BODY_A+0x64,cache2);m.run(0x469810,ecx=n.BODY_A,budget=3000000)
 created=[i for i,p in enumerate(shape_rows) if m.get(p+0x10)]
 assert created==list(range(5,53))
 for p in shape_rows:
  body=m.get(p+0x10)
  if body:m.run(0x46a580,(1,),ecx=body,budget=3000000)
 m.put(n.BODY_A+0x64,original_cache)
 return dict(cache_cases=len(rows),new_shape_admission=created)
r=episode(Image(raw),n,inputs(root),jdlz(track),(4,.7,.2),(0,0,0),(0,0,0),world_setup=cache_setup,world_queue=True)
assert r['success']
assert (root/'speed.exe').read_bytes()==raw and (root/'Tracks/TRACKB1003.lzc').read_bytes()==track
out.write_text(json.dumps(dict(cases=rows,shape_admission=r['world_setup']['new_shape_admission'],complete=True,limits='Original cache producer and query; explicit poses, no live race.'),indent=2)+'\n',encoding='ascii')
print('PASS',len(rows),'original cache updates and53-wall admission; retail unchanged')
