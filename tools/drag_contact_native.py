"""Actual465D00 drag failure latch, with supplied contact/body/actor views.

The actor starts in collision state1 to isolate the independent failure
branch from normal-mode extra impulses. Optional visual owner is absent.
Both drag-mode flags, physical selectors, ordinary bodies and the two special
object hashes are covered without hooks in that latch witness. The separate
whole numeric solver witness declares its effects-tail hook. Neither is an
original world construction or complete race replay.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,itertools
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true')
ap.add_argument('--out',type=Path)
a=ap.parse_args()
raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
vectors=[]
for threshold in (15.,35.):
 for word in (bits(threshold)-1,bits(threshold),bits(threshold)+1):
  value=struct.unpack('<f',struct.pack('<I',word))[0]
  vectors.append((value,0.,0.))
vectors.extend(((21.,28.,0.),(0.,21.,28.),(10.,10.,10.),(100.,0.,0.),(0.,0.,0.)))
for mode,selector,kind,name,j in itertools.product(range(3),(1,2,3,4),(0,3,4),(0,0x24cebc53,0x427a6453),vectors):
 m=Machine(image);m.allowed=[(0x465d00,0x466350)]
 record,pair,body,other,actor,peer,component,engine,descriptor,point,impulse=range(0x1100000,0x110b000,0x1000)
 m.put(record+0x64,pair);m.put(pair+0x258,body);m.put(pair+0x25c,other)
 m.put(body+0x15,0,'B');m.put(other+0x15,kind,'B')
 m.put(body+0x140,actor);m.put(other+0x140,peer if kind==0 else descriptor)
 m.put(descriptor+0xc,name)
 for car,physical in ((actor,selector),(peer,2)):
  m.put(car+0x28,physical);m.put(car+0x38,1);m.put(car+0x34,component);m.put(car+0x868,0)
 m.put(component+0x48,engine)
 for k,v in enumerate(j):m.put(impulse+4*k,v,'f')
 m.put(0x78a344,int(mode==1),'B');m.put(0x7361a8,int(mode==2))
 m.run(0x465d00,(record,point,impulse))
 assert not m.events
 rows.append([mode,selector,kind,name,*[bits(v)for v in j],m.get(actor+0x874,'B'),m.get(engine+0x8c)])
def lit(v):return '{'+','.join(map(str,v))+'},\n'
text='// Actual465D00 drag failure latch and engine heat write, declared views.\n'
text+='static constexpr unsigned kNativeDragContact[][9]={\n'+''.join(lit(r)for r in rows)+'};\n'
# Whole numeric solver/callback: retain the normal witness's explicit effects
# tail boundary, but execute the actual drag branch and extra impulses.
import contact256_native as n
base_fixture=n.fixture;callbacks=[]
for mode,offset,va,vb in itertools.product((0,1),((2.0005,.7,.2),(2.0005,0.,0.)),((50.,0.,0.),(30.,20.,0.)),((-50.,0.,0.),)):
 machines=[]
 def fixture(*args,**kwargs):
  m=base_fixture(*args,**kwargs)
  m.put(0x78a344,mode,'B')
  for index,actor in enumerate((n.ACTOR_A,n.ACTOR_B)):
   m.put(actor+0x28,1);m.put(actor+0x868,0)
   component=0x1110000+index*0x1000;engine=0x1112000+index*0x1000
   m.put(actor+0x34,component);m.put(component+0x48,engine)
   physical=n.CONFIG+0x300+index*0x100
   m.put(actor+0x20,physical);m.put(physical+0x14,-1.,'f');m.put(physical+0x44,1.,'f')
  machines.append(m);return m
 n.fixture=fixture
 try:case=n.contact_case(image,'drag-mode-'+str(mode),offset,va,vb)
 finally:n.fixture=base_fixture
 m=machines[0]
 assert all(x['hook']=='skip_current_hit_effect_tail_4658B5_to_465CF2'for x in case['hooks'])
 def before(body):
  matrix=body['inverse_inertia']
  return [bits(body['inverse_mass']),body['state'],bits(body['state_timer']),*[bits(v)for key in('position','velocity','angular_velocity','linear_momentum','angular_momentum')for v in body[key]],*[bits(matrix[4*j+k])for j in range(3)for k in range(3)]]
 def after(body):return [*[bits(v)for key in('linear_momentum','angular_momentum')for v in body[key]],body['state'],bits(body['state_timer'])]
 callbacks.append([mode,[before(b)for b in case['before']],[bits(v)for key in('normal','point')for v in case['geometry'][key]],[after(b)for b in case['after']],m.get(n.CONFIG+0x12d0)])
def nested(v):return '{'+','.join(map(nested,v))+'}'if isinstance(v,list)else str(v)+'U'
text+='struct NativeDragCallback {unsigned drag,before[2][27],geometry[6],after[2][8],seed;};\n'
text+='static constexpr NativeDragCallback kNativeDragCallback[]={\n'+''.join(nested(r)+',\n'for r in callbacks)+'};\n'
p=Path(__file__).resolve().parents[1]/'tests/drag_contact_native.inc'
if a.write_fixture:p.write_text(text,encoding='ascii')
else:assert p.read_text('ascii')==text,'original drag collision latch differs'
if a.out:
 assert a.out.resolve().relative_to(Path.cwd()).parts[0].startswith('build')and not a.out.exists()
 a.out.write_text(json.dumps(dict(scope=__doc__,rows=rows,callbacks=callbacks,hooks=['skip_current_hit_effect_tail_4658B5_to_465CF2; full numeric callback executes']),indent=2)+'\n',encoding='ascii')
print('PASS actual drag collision latch/heat, cases='+str(len(rows)))
