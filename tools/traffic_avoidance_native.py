"""Original strategy4 avoidance control flow, with declared external queries.

Runs 453920,453A60,453BA0,453C60 through the path-publication boundary.
44F470 query outputs,44EFD0 admission,5A7700 transition and456E90 nearest
lane are supplied callback inputs, not a claim of natural geometry parity.
Their actual geometry is covered by the separate native query/context tools.
No control branch or arithmetic under comparison is replaced.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
from unicorn.x86_const import UC_X86_REG_EIP
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail_root',type=Path)
ap.add_argument('--write-fixture',action='store_true')
ap.add_argument('--out',type=Path)
a=ap.parse_args();root=Path(__file__).resolve().parents[1]
raw=(a.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw)
ACTOR,CONTROLLER,CTX,ACTION,CHAIN,CELL,VTABLE,STRATEGY,OBJ,OTHER,CONFIG,OUT=range(0x1100000,0x110c000,0x1000)
TARGET,TARGET2=0x110d000,0x110d400
profiles=[]
def profile(**kw):
 d=dict(category=11,present=1,distance=160.,lateral_velocity=0.,relative_velocity=-5.,speed=20.,time=0.,clearance=30.,nearest=11,traffic=1,admit=1,lanes=63,self_length=2.,target_length=2.,queries=[[0,99999.,99999.] for _ in range(6)])
 d.update(kw);return d
profiles.append(profile())
for dist in (10.,15.,15.0001,20.,50.):
 profiles.append(profile(queries=[[TARGET,0.,dist] for _ in range(6)]))
for relative in (-40.001,-40.,-39.999,5.,5.001):
 profiles.append(profile(relative_velocity=relative,queries=[[TARGET,2.,25.]]+[[0,99999.,99999.] for _ in range(5)]))
for time in (1.49999,1.5,5.,5.0001):
 profiles.append(profile(relative_velocity=-40.,queries=[[TARGET,time,40.] for _ in range(6)]))
for gap in (29.9999,30.,30.0001):
 profiles.append(profile(distance=104.+gap,relative_velocity=-40.,queries=[[TARGET,3.,40.] for _ in range(6)]))
for category in (11,12,15,16):
 for lateral in (-1.,1.):
  q=[[0,99999.,99999.] for _ in range(6)];q[category-11]=[TARGET,0.,10.]
  profiles.append(profile(category=category,lateral_velocity=lateral,queries=q))
for speed in (0.,.49999,.5):
 profiles.append(profile(speed=speed,relative_velocity=-40.,queries=[[TARGET,3.,50.] for _ in range(6)],nearest=12))
profiles.extend([profile(present=0),profile(distance=80.,queries=[[TARGET2,2.,40.] for _ in range(6)]),profile(admit=0,queries=[[TARGET,0.,10.] for _ in range(6)]),profile(traffic=0,queries=[[TARGET,0.,10.] for _ in range(6)]),profile(lanes=0,queries=[[TARGET,0.,10.] for _ in range(6)])])
rows=[]
for mode in range(3):
 for p in profiles:
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)];calls=[]
  m.put(ACTOR+0x2c,CONTROLLER);m.put(ACTOR+0x3d4,p['speed'],'f');m.put(CONTROLLER,0x6b96b0);m.put(CONTROLLER+0x4dc,24.58650016784668,'f');m.put(CONTROLLER+0x3fc,STRATEGY);m.put(STRATEGY,VTABLE);m.put(VTABLE+0xc,0x5314d0)
  m.put(CTX,ACTOR);m.put(CTX+4,2 if p['present'] else 1);m.put(CTX+0x388,1 if p['present'] else 0,'B');m.put(CTX+0x394,p['category']);m.put(CTX+0x398,2);m.put(CTX+0x3a8,CHAIN)
  m.put(CHAIN,VTABLE+0x100);m.put(VTABLE+0x104,0x44d4a0);m.put(CHAIN+8,CELL);m.put(CELL+0x5c,255,'B');m.put(CELL+0x5e,65535,'H')
  m.put(ACTION+0x200,TARGET);m.put(ACTION+0x204,p['time'],'f');m.put(ACTION+0x208,p['clearance'],'f')
  index=0
  if p['present']:
   entry=CTX+8;m.put(entry,TARGET);m.put(entry,p['lateral_velocity'],'f');m.put(entry+4,p['relative_velocity'],'f');m.put(entry+8,1.,'f');m.put(entry+12,p['target_length'],'f');m.put(entry+16,p['distance'],'f');m.put(entry+52,TARGET);index=1
  entry=CTX+8+index*56;m.put(entry+8,1.,'f');m.put(entry+12,p['self_length'],'f');m.put(entry+16,100.,'f');m.put(entry+52,ACTOR)
  for j in range(6):m.put(entry+24+4*j,j*.125,'f')
  for target in (TARGET,TARGET2):m.put(target+8,6);m.put(target+0x1d4,OBJ)
  m.put(OBJ+0x24,6);m.put(OBJ+0x90,OTHER);m.put(OTHER+0x14,CONFIG);m.put(CONFIG+4,3 if p['traffic'] else 2)
  def query(x):
   sp=x.reg('esp');category=x.get(sp+4,'i');offset=x.get(sp+8);clearance=x.get(sp+12);t=x.get(sp+16);d=x.get(sp+20)
   assert 11<=category<=16,category
   q=p['queries'][category-11];calls.append([category,offset,clearance]);x.put(t,q[1],'f');x.put(d,q[2],'f');x.ret(0,q[0])
  def admitted(x):x.ret(0,p['admit'])
  def transition(x):
   category=x.reg('eax');x.ret(8,int(11<=category<=16 and bool(p['lanes']&(1<<(category-11)))))
  def nearest(x):x.ret(0,p['nearest']&0xffffffff)
  m.hook(0x44f470,'declared-obstacle-query',callback=query);m.hook(0x44efd0,'declared-intersection-admission',callback=admitted)
  m.hook(0x5a7700,'declared-lane-transition',callback=transition);m.hook(0x456e90,'declared-nearest-lane',callback=nearest)
  if mode==0:
   m.run(0x453920,(OUT,OUT+4,OUT+8),esi=CTX);value=int(m.reg('eax')==300)
   if value:m.put(ACTION+0x200,m.get(OUT));m.put(ACTION+0x204,m.get(OUT+4));m.put(ACTION+0x208,30.,'f')
  elif mode==1:
   m.run(0x453a60,(CTX,),ecx=ACTION);value=m.reg('eax')&255
  else:
   m.run(0x453c60,(CTX,),ecx=ACTION,end=0x454009,budget=2000000);assert m.uc.reg_read(UC_X86_REG_EIP)==0x454009;value=0
  expected=[value,m.get(CTX+0x394),m.get(ACTION+0x200),m.get(ACTION+0x204),m.get(ACTION+0x208)]
  if mode==2:
   enabled=m.get(ACTOR+0x88c,'B');expected +=[m.get(ACTION+off) for off in (0x1e4,0x1e8,0x1ec)]+[enabled]+[m.get(ACTOR+0x890+4*j)if enabled else 0 for j in range(4)]+[int(m.get(CTX+0x398)==1)]
  else:expected +=[0]*9
  assert len(expected)==14
  rawin=[bits(p[k]) for k in ('distance','lateral_velocity','relative_velocity','speed','time','clearance','self_length','target_length')]+[p[k] for k in ('nearest','traffic','admit','lanes')]
  queries=[[q[0],bits(q[1]),bits(q[2])] for q in p['queries']]
  rows.append(dict(mode=mode,category=p['category'],present=p['present'],raw=rawin,queries=queries,expected=expected,calls=calls))
def literal(v):return '{'+','.join(literal(x) if isinstance(x,list)else '0x%08xU'%x for x in v)+'}'
text='// Actual strategy4 avoidance control, declared external query/admission inputs.\nstatic constexpr NativeTrafficAvoidance kNativeTrafficAvoidance[]={\n'
for r in rows:
 h=2166136261
 for call in r['calls']:
  for value in call:h=((h^value)*16777619)&0xffffffff
 text+=' '+literal([r['mode'],r['category'],r['present'],r['raw'],r['queries'],r['expected'],len(r['calls']),h])+',\n'
text+='};\n'
end_rows=[]
for flag in (0,1):
 for distance in (-1.,0.,10.,19.9999,20.,20.0001,30.,31.):
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.put(CHAIN+4,4);m.put(CHAIN+0xbc,flag,'B')
  for i in range(4):m.put(CHAIN+8+4*i,CELL+0x100*i);m.put(CELL+0x100*i+0x58,float(i*10),'f')
  m.run(0x5a6ce0,(bits(distance),),esi=CHAIN);end_rows.append([flag,bits(distance),m.reg('eax')])
text+='// Actual5A6CE0 with declared compact coordinates0,10,20,30.\nstatic constexpr unsigned kNativeTrafficStopEvent[][3]={\n'
for r in end_rows:text+=' '+literal(r)+',\n'
text+='};\n';target=root/'tests/traffic_avoidance_native.inc'
if a.write_fixture:target.write_text(text,'ascii')
else:assert target.read_text('ascii')==text,'traffic avoidance native fixture differs'
if a.out:
 rel=a.out.resolve().relative_to(root);assert rel.as_posix().isascii() and rel.parts[0].startswith('build');assert not a.out.exists();a.out.write_text(json.dumps({'rows':rows,'stop_events':end_rows,'boundary':__doc__},indent=2)+'\n','ascii')
print('PASS original traffic avoidance control flow, cases='+str(len(rows))+'; declared geometry boundaries')
