# Controlled two-body original queue. Input handling/hulls and synthetic wall only.
from pathlib import Path
import sys,json,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image,jdlz
from event0_trace import chunks
from contact257_retail import inputs
from contact257_episode import episode,TRACK_SHA
import contact256_native as n
if len(sys.argv)!=3:raise SystemExit('usage: race_ground_contact_native.py RETAIL OUTPUT')
root=Path(sys.argv[1]);out=Path(sys.argv[2])
assert out.resolve()!=root.resolve() and root.resolve() not in out.resolve().parents
out.mkdir(parents=True,exist_ok=False)
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);track=(root/'Tracks/TRACKB1003.lzc').read_bytes()
assert hashlib.sha256(track).hexdigest().upper()==TRACK_SHA
decoded=jdlz(track)
# Explicit UG0 proving fixture selections; handling and CVs come from retail.
selected=[dict(name=name,id=ident,levels=[level]*11,geometry='Cars/'+name+'/GEOMETRY.BIN')
 for name,ident,level in [('INTEGRA',14,1),('ECLIPSE',7,0),('CELICA',6,0),('SUPRA',9,0),
 ('4DR_SEDAN02',21,0),('TAXI02',23,0),('PANELVAN',24,0),('PICKUP',28,0),('SUV',31,0),('PARCELVAN',27,0)]]
source_files=['speed.exe','Global/GLOBALB.BUN','Tracks/TRACKB1003.lzc']+[a['geometry'] for a in selected]
def identities():
 return {name:dict(size=(root/name).stat().st_size,sha256=hashlib.sha256((root/name).read_bytes()).hexdigest()) for name in source_files}
before=identities()
rows=[]
cases=[(side,wall,index,1) for side,wall in (('front',1.75),('back',-2.)) for index in range(1,10)]
cases += [(side,wall,index,-1) for side,wall in (('reverse-front',-1.75),('reverse-back',2.)) for index in (1,6)]
for side,wall,index,sign in [(side,wall,index,1) for side,wall in (('floor-front',1.75),('floor-back',-2.)) for index in (1,2,3,8)]:
 actors=(selected[0],selected[index]);machine=[None]
 def configure(m):
  machine[0]=m
  m.put(0x73619c,0x1141000)
  m.put(0x1141000,4)
  m.put(n.ACTOR_A+0xc,0x1140000)
  m.put(n.ACTOR_A+0x18,0x1140000)
  from unicorn.x86_const import UC_X86_REG_EIP
  def free(x):
   p=x.get(x.reg('esp')+8)
   assert 0x2a00000<=p<0x2f00000
   x.events[-1]['pointer']=hex(p)
   x.reg('esp',x.reg('esp')+12);x.reg('eax',1)
   x.uc.reg_write(UC_X86_REG_EIP,0x67116d)
  m.hook(0x671167,'external_HeapFree_fixture_no_reuse',callback=free)
  for a in (0x735fa0,0x7361a4):m.put(a,0)
  m.put(0x78a345,0,'B')
  records=[]
  for k,(actor,ride) in enumerate(zip(actors,(0x1120000,0x1121000))):
   m.run(0x417600,edx=ride)
   m.run(0x57D160,(0,0,0),edx=ride,eax=actor['id'])
   for i,v in enumerate(actor['levels']):m.put(ride+8+4*i,v)
   m.run(0x57F2B0,(0,),edi=ride,budget=3000000)
   # Keep the full57F2B0 result, as the human PhysMov constructor does.
   blob=(root/actor['geometry']).read_bytes();address=0x4000000+k*0x1000000
   m.uc.mem_map(address,(len(blob)+4095)&~4095);m.uc.mem_write(address,blob)
   count=0
   for tag,at,end in chunks(blob):
    if tag==0x80034020:m.run(0x472B00,(address+at,));count+=1
   assert count==1
   records.append(dict(name=actor['name'],levels=actor['levels'],geometry_sha256=hashlib.sha256(blob).hexdigest(),mass=m.get(ride+0x170,'f'),com=n.vector(m,ride+0x60),inertia=[m.get(ride+0x180+20*i,'f') for i in range(3)]))
  return records
 def world_setup(m):
  import struct
  raw=0x1150000
  # Match the port fixture's active rival controls: first gear, automatic.
  # The raw factory otherwise retains neutral/request1; that is not a
  # comparable driven-wheel state after the second impact.
  if index<4:
   engine=m.get(m.get(n.ACTOR_B+0x34)+0x24)
   m.put(engine+0x124,2);m.put(engine+0x134,0xffffffff)
  # Controlled front/back vertical wall plus decoded flat wheel ground.
  corners=((wall,-8,-8),(wall,-8,8),(wall,8,8),(wall,8,-8))
  record=bytes((0,0x80,0,0x10))+struct.pack('<hH',-8,65535)
  record+=struct.pack('<4h',*[int(p[0]*8) for p in corners])
  record+=struct.pack('<4h',*[int(p[1]*8) for p in corners])
  record+=struct.pack('<4h',*[int((p[2]+8)*256) for p in corners])
  assert len(record)==32
  m.run(0x4412b0,(0x77b130,),budget=3000000)
  m.run(0x445720,esi=0x77b180)
  m.uc.mem_write(raw,record)
  m.run(0x441b90,(0x77b130,),eax=raw,budget=3000000)
  polygon=m.reg('eax')
  floor_corners=((-16,-16,-.375),(16,-16,-.375),(16,16,-.375),(-16,16,-.375))
  floor_raw=raw+32
  floor_record=bytes((0,0xa0,0,0x10))+struct.pack('<hH',-1,65535)
  floor_record+=struct.pack('<4h',*[int(p[0]*8) for p in floor_corners])
  floor_record+=struct.pack('<4h',*[int(p[1]*8) for p in floor_corners])
  floor_record+=struct.pack('<4h',*[int((p[2]+1)*256) for p in floor_corners])
  m.uc.mem_write(floor_raw,floor_record)
  m.run(0x441b90,(0x77b130,),eax=floor_raw,budget=3000000)
  floor=m.reg('eax')
  for b in (n.BODY_A,n.BODY_B):
   cache=m.get(b+0x64);m.put(cache+0x20,polygon);m.put(cache+0x24,floor);m.put(cache+0x2a0,2);m.put(cache+0x2a4,2)
   for k,value in enumerate((-16.,-16.,16.,16.)*2):m.put(cache+4*k,value,'f')
   m.run(0x468ec0,esi=b,budget=3000000)
  return dict(corners=corners,floor=floor_corners,polygon=hex(polygon),floor_polygon=hex(floor))

 try:
  r=episode(im,n,data,decoded,(4.*sign,.7,.2),(30.*sign,0.,0.),(-10.*sign,0.,0.),history=[dict(duration=1/60,position=(-.5*sign,0.,0.),velocity=(30.*sign,0.,0.),axes=((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))),dict(duration=1/60,position=((4.+1/6)*sign,.7,.2),velocity=(-10.*sign,0.,0.),axes=((1.,0.,0.),(0.,1.,0.),(0.,0.,1.)))],configure=configure,selectors=(1,3 if index<4 else 2),world_setup=world_setup,world_queue=True,warm_ground=True)
  r['name']=actors[1]['name'];rows.append(r)
  print(r['name'],r['phase'],len(r['hits']),r['selected_prerequisites'],flush=True)
  (out/('all-world-'+side+'-'+str(index)+'.json')).write_text(json.dumps(r,indent=2),encoding='ascii')
 except Exception as e:
  print('FAILED',actors[1]['name'],repr(e),flush=True)
  m=machine[0];print('regs',[(k,hex(m.reg(k))) for k in ('eax','ebx','ecx','edx','esi','edi','esp')]);print('last',list(m.executed.items())[-25:]);print('stack',[hex(m.get(m.reg('esp')+i*4)) for i in range(12)]);raise

if identities()!=before:raise RuntimeError('retail input changed during native comparison')
report=dict(inputs=before,selected=selected,fpu_control_word='007F',cases=len(rows),
 limits='Two-body synthetic poses, floor and wall; unmodified native ordinary grip, warm human neutral and rival first/automatic. No live race, spawning, AI difficulty or full trajectory proof.',
 boundaries='Single-thread critical sections, zero-QPC, unavailable DSOUND service, controlled heap frees without reuse.',
 completed=all(r['success'] for r in rows))
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
if not report['completed']:raise RuntimeError('incomplete original execution')
print('PASS: original world queue completed all',len(rows),'cases; retail inputs unchanged')
