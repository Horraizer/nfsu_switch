"""Actual traffic44CF30 context on initialized read-only PC1.4 USA data.

The isolated stock240SX actor uses original582F90/583700 constructors, role3,
physical selector3, owner kind4 and trafficID50. Exogenous poses/category/maneuver inputs
exercise retained roads, junction passage, unavailable lane and road re-entry.
External heap/timer IO is declared by ActorController; no geometry, category,
road-policy or compact-cell output is hooked. This is not natural spawn,
shared-world scheduling, integrated traffic actions/contacts or physics.
"""
from pathlib import Path
import sys,hashlib,json,capstone,argparse,struct
sys.dont_write_bytecode=True
from controller293_planner import ActorController,inputs,CTX
from controller293_strategy import CONTROLLER
from physics256_native import ACTOR
from world_body_trace import Image
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
args=ap.parse_args();raw=(args.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7','unsupported executable'
image=Image(raw);actor=ActorController(image,inputs(args.retail),traffic=True);m=actor.m
assert m.get(CONTROLLER)==0x6b96b0 and m.get(m.get(ACTOR+0x14)+4)==3
assert m.get(ACTOR+0x1c)==0 and m.get(ACTOR+0x24)==4 and m.get(ACTOR+0x28)==3
assert m.get(ACTOR+0xa,'h')==50
from controller293_native import EXEC_RANGES
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for a,b in list(EXEC_RANGES)+[(0x583700,0x583850),(0x583850,0x583910),(0x583910,0x583b20),(0x583b20,0x583bb0),(0x583bb0,0x583c00),(0x5832a0,0x5832e0),(0x5832e0,0x583350),(0x583c00,0x583c80),(0x452800,0x452870),(0x4528b0,0x452d10),(0x45cff0,0x45d250),(0x45ae10,0x45afd0),(0x45afd0,0x45b050),(0x44d4d0,0x44d550),(0x5a6150,0x5a6306),(0x5a7390,0x5a7449),(0x5a6d20,0x5a6e0f),(0x5a7007,0x5a7014),(0x57b31c,0x57b332),(0x57b358,0x57b369),(0x442690,0x442980),(0x44d410,0x44d420),(0x456e90,0x457060),(0x44c940,0x44ca70),(0x44ace8,0x44acf5),(0x583240,0x5832a0),(0x5a75c0,0x5a7690),(0x45ad30,0x45ae10),(0x57a8e0,0x57a910),(0x45c640,0x45c75e),(0x5a7270,0x5a7340),(0x5a6bc0,0x5a6ce0),(0x5a6b00,0x5a6bc0),(0x5a7690,0x5a7750),(0x454f30,0x455010),(0x4550c0,0x4550d0),(0x582f70,0x582f90),(0x583350,0x5835f0),(0x583da0,0x583e80),(0x44ca70,0x44cb80),(0x5a71f0,0x5a7270),(0x458cc0,0x458e60),(0x5835f0,0x583690),(0x5a6ce0,0x5a6d20),(0x44e020,0x44e260),(0x4543d0,0x454420),(0x453920,0x453a60),(0x583c80,0x583da0),(0x5a67d0,0x5a6860),(0x44f470,0x44f6c0),(0x44f700,0x44f780)]:
 m.instructions.update({i.address:i.size for i in md.disasm(Image(raw).read(a,b-a),a)})

m.instructions.update({i.address:i.size for i in md.disasm(Image(raw).read(0x5a5c30,0x5a5eeb-0x5a5c30),0x5a5c30)})
m.instructions.update({i.address:i.size for i in md.disasm(Image(raw).read(0x5a6f80,0x5a701b-0x5a6f80),0x5a6f80)})
m.instructions.update({i.address:i.size for i in md.disasm(Image(raw).read(0x5a6000,0x80),0x5a6000)})
from unicorn import UC_HOOK_CODE
from ai_path_vectors import literal
CTX=CONTROLLER+0x2c
for start,end in [(0x44CF30,0x44D3D4),(0x44ADC0,0x44B060),(0x44C250,0x44C550),
                  (0x582F40,0x582F90),(0x583690,0x583700),(0x45C550,0x45C636)]:
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(start,end-start),start)})
choice=None
for route_id in range(100):
 route=m.get(0x77af30+4*route_id)
 if not route or not any(m.get(route+0x270+k,'B')for k in range(6)):continue
 eligible=[i for i in range(m.get(route+0x10,'h'))if m.get(route+0x27c+i*0x88+0x84)&0x2000000]
 if eligible:choice=(route_id,eligible[len(eligible)//2]);break
assert choice
fixture_route,point=choice;actor.pose(point,20.,route_id=fixture_route)
m.run(0x583700,ecx=CONTROLLER,budget=3000000)
chain=m.get(CTX+0x3a8);world=m.get(0x7361f8)

def snapshot():
 route=m.get(CTX+0x38c);raw=m.get(CTX+0x390)
 state=dict(selected=bool(route),road=[m.get(route+0xa,'h'),(raw-route-0x27c)//0x88]if route else[0,0],
 category=m.get(CTX+0x394,'i'),nearest=m.get(CONTROLLER+0x4a4,'b'),origin=m.get(chain+0xac,'i'),cells=[],
 rng=m.get(world+0x2d0),cache=[m.get(CONTROLLER+0xc+4*k)for k in range(8)],marker=m.get(CONTROLLER+0x478,'i'),
 flags=[bool(m.get(chain+0xb0+k,'B'))for k in range(7)],limited=m.get(chain+0xb8,'i'),obstacle=m.get(chain+0xc0,'i'),event=bool(m.get(chain+0xbc,'B')),maneuver=[m.get(CTX+0x398),m.get(CTX+0x39c),m.get(CTX+0x3a0),m.get(CTX+0x3a4,'B')])
 for j in range(m.get(chain+4)):
  c=m.get(chain+8+4*j);assert m.get(c)==0x6c8f90
  state['cells'].append([m.get(c+0x5c,'b'),m.get(c+0x5e,'h'),m.get(c+0x58),bool(m.get(c+0x60,'B')),*[[m.get(c+at+4*k)for k in range(7)]for at in(4,0x20,0x3c)]])
 return state
store,result=0x119f100,0x119f000;code=b'\xd9\x1d'+__import__('struct').pack('<I',result)+b'\xc3'
m.uc.mem_write(store,code);m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
def scalar(address,**regs):
 m.run(address,budget=300000,**regs);m.run(store,budget=20);return m.get(result,'f')
def virt(slot):return m.get(m.get(CONTROLLER)+slot)
observed={}
def observe(uc,address,size,user):
 if address==0x44d2f4:observed['after']=snapshot()
 if address==0x44c250:observed['reselect']=True
 if address==0x44d128:observed['clear']=True
 if address==0x44d0e5:
  event=m.reg('eax');observed['passed'].append((event-m.get(0x736150))//32 if event else -1)
 if address==0x44d23f:observed['category_available']=bool(m.reg('eax')&255)
 if address==0x44d337:observed['lane_reset']=True
m.uc.hook_add(UC_HOOK_CODE,observe)
records=[]
poses=[(21,29,0,False),(21,30,0,False),(21,40,0,False),(21,50,0,False),(23,15,0,True),(34,50,0,True),(35,50,0,True),(35,55,15,True),(35,56,-15,True),(36,40,0,True),(39,15,0,True),(41,15,0,True),(21,29,10000,False),(21,29,0,True)]
for sequence,(rid,p,lateral,cold)in enumerate(poses):
 route=m.get(0x77af30+4*rid)
 assert 0<=p<m.get(route+0x10,'h')
 actor.pose(p,20.,route_id=rid)
 rawpoint=route+0x27c+p*0x88
 for k,f in enumerate((m.get(rawpoint+0x10,'f'),-m.get(rawpoint+0xc,'f'))):m.put(ACTOR+0x60+4*k,m.get(ACTOR+0x60+4*k,'f')+lateral*f,'f')
 m.run(0x57a380,esi=m.get(m.get(ACTOR+0x82c)+0x20))
 if cold:m.run(0x5a5340,esi=chain,budget=300000)
 m.put(0x73ad34,100000+80*sequence)
 if sequence==3:m.put(CONTROLLER+0x478,5)
 if sequence in(1,5,9):m.put(CTX+0x394,16)
 if sequence==1:
  m.put(CTX+0x398,3);m.put(CTX+0x39c,123);m.put(CTX+0x3a0,2.,'f');m.put(CTX+0x3a4,1,'B')
 before=snapshot();position=[m.get(ACTOR+0x60+4*k,'f')for k in range(3)]
 body=m.get(ACTOR+0x3c);width=m.get(body+0x104,'f')-m.get(body+0xf4,'f')
 m.run(virt(0x24),ecx=CONTROLLER);keep=bool(m.reg('eax')&255)
 m.run(virt(0x64),ecx=CONTROLLER);allow=bool(m.reg('eax')&255)
 initial=max(m.get(0x700680,'f'),scalar(virt(0x50),ecx=CONTROLLER)*20.)
 retained=scalar(0x458b90,ecx=CONTROLLER)
 scratch=0x1150600;m.run(virt(0x54),(scratch,scratch+4,scratch+8),ecx=CONTROLLER)
 selection=[position,m.get(ACTOR+0x814,'f'),m.get(0x6b7704,'f'),m.get(scratch,'i'),m.get(scratch+4,'i'),m.get(CONTROLLER+0x4d4,'B'),3,-1,bool(m.get(scratch+8,'B')),True]
 observed.clear();observed.update(reselect=False,clear=False,passed=[],category_available=False,lane_reset=False)
 m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
 assert 'after'in observed
 records.append(dict(sequence=sequence,before=before,after=observed['after'],selection=selection,values=[width,m.get(0x6ccc30,'f'),initial,retained],keep=keep,allow=allow,ride_category=m.get(actor.ride+0x424,'b'),controller_kind=m.get(ACTOR+0x24),notifications={k:v for k,v in observed.items()if k!='after'}))

aliases=[]
for rid in range(128):
 route=m.get(0x77af30+4*rid)
 if not route:continue
 for i in range(m.get(route+0x10,'h')):
  p=route+0x27c+i*0x88;ids=[m.get(p+at,'B')for at in(0x81,0x82)]
  if ids!=[255,255]:aliases.append([rid,i,*ids])
events=[];base=m.get(0x736150)
for j in range(m.get(0x736154)):
 at=base+j*32;events.append([m.get(at,'f'),m.get(at+4,'H')]+[m.get(at+k,'B')for k in(8,9,11,13,15,16)]+[m.get(at+6,'H'),m.get(at+10,'B'),m.get(at+12,'B')])
def lit(v):
 if isinstance(v,bool):return str(v).lower()
 if isinstance(v,(list,tuple)):return '{'+','.join(map(lit,v))+'}'
 if isinstance(v,float):return literal(v)
 return str(v)
def snap(s):return lit([s['selected'],s['road'],s['category'],s['nearest'],s['origin'],len(s['cells']),s['cells'],s['rng'],s['cache'],s['marker'],s['flags'],s['limited'],s['obstacle'],s['event']])
text='// Actual traffic context; declared original constructors and exogenous state.\n'
text+='struct NativeTrafficContextCell {int route;unsigned point,distance;bool junction;unsigned offset[7],lower[7],upper[7];};\n'
text+='struct NativeTrafficContextSnapshot {bool selected;AiRoadCursor road;int category,nearest,origin;unsigned count;NativeTrafficContextCell cells[40];unsigned rng,cache[8];int marker;bool flags[7];int limited,obstacle;bool event;};\n'
text+='struct NativeTrafficContextCase {NativeTrafficContextSnapshot before,after;AiRoadContextInput input;bool reselected,clear,available,reset;unsigned passed_count;short passed[40];};\n'
text+='struct NativeTrafficContextPointEvent {int route;unsigned point;unsigned a,b;};\n'
text+='static const NativeTrafficContextPointEvent kTrafficContextPointEvents[]={\n'+''.join(lit(r)+',\n'for r in aliases)+'};\n'
text+='static const AiRoadEvent kTrafficContextEvents[]={\n'+''.join(lit(r)+',\n'for r in events)+'};\n'
text+='static const NativeTrafficContextCase kNativeTrafficContext[]={\n'
for r in records:
 n=r['notifications']
 text+='{'+','.join([snap(r['before']),snap(r['after']),lit([r['selection'],*r['values'],r['keep'],r['allow'],r['ride_category'],r['controller_kind']]),lit(n['reselect']),lit(n['clear']),lit(n['category_available']),lit(n['lane_reset']),str(len(n['passed'])),lit(n['passed'])])+'},\n'
text+='};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/traffic_context_native.inc'
assert text.isascii()
if args.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'actual traffic context fixture differs'
if args.out:
 relative=args.out.resolve().relative_to(Path.cwd());assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
 args.out.mkdir(exist_ok=False)
 (args.out/'oracle.json').write_text(json.dumps(dict(scope=__doc__,records=records,events=events,aliases=aliases),indent=2)+'\n',encoding='ascii')
assert records[1]['notifications']['lane_reset'] and records[1]['after']['maneuver']==[1,0,0,0]
assert records[3]['after']['marker']==5
assert records[-2]['after']['category']==-1 and records[-1]['after']['category']>=0
print('PASS actual isolated traffic road context, nearest lane, maneuver reset, passage and re-entry')
