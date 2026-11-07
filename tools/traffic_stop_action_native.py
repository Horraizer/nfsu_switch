"""Actual454170/454490 traffic stop action, with transparent query observations.

Authenticated initialized owner4/ID50/physical3, exogenous poses, compact
self distances and intersection states. Both a self-only clear scene and a
real registered static second stock shape execute actual query/path bodies.
Output controls/reservation/latch/actor override are compared; natural spawn,
world scheduling, action lifecycle and physical recovery are not certified.
"""
from pathlib import Path
import argparse,sys,runpy,capstone,json,itertools
sys.dont_write_bytecode=True
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();argv=sys.argv[:];sys.argv=['tools/traffic_context_native.py',str(options.retail)]
s=runpy.run_path(str(Path(__file__).resolve().parent/'traffic_context_native.py'));sys.argv=argv
actor,m,image,scalar=(s[k]for k in('actor','m','image','scalar'))
from physics256_native import ACTOR,bits
from controller293_strategy import CONTROLLER
from unicorn import UC_HOOK_CODE
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
ranges=[(5781248, 5781584), (5781584, 5781776), (5781776, 5782304), (5782304, 5782448), (5782448, 5782528), (5780128, 5780192), (5780192, 5780304), (5782528, 5782656), (4532224, 4532336), (4532400, 4533520), (4575216, 4575824), (4566544, 4566992), (4566992, 4567120), (4510928, 4511056), (5923152, 5923590), (5927824, 5928009), (5926176, 5926415), (5926919, 5926932), (5747484, 5747506), (5747544, 5747561), (4466320, 4467072), (4510736, 4510752), (4550288, 4550752), (4507968, 4508272), (4500712, 4500725), (5780032, 5780128), (5928384, 5928592), (4566320, 4566544), (5744864, 5744912), (4572736, 4573022), (5927536, 5927744), (5925824, 5926112), (5925632, 5925824), (5928592, 5928784), (4542256, 4542480), (4542656, 4542672), (5779312, 5779344), (5780304, 5780976), (5782944, 5783168), (4508272, 4508544), (5927408, 5927536), (4558016, 4558432), (5780976, 5781136), (5926112, 5926176), (4513824, 4514400), (4539344, 4539424), (4536608, 4536928), (5782656, 5782944), (5924816, 5924960), (4519024, 4519616), (4519680, 4519808), (4518320, 4519020), (4608208, 4608321), (4607776, 4608208), (5730096, 5730256), (5736240, 5736496), (5738944, 5739456), (5743424, 5743440), (4663968, 4664064), (4666272, 4666464), (4242752, 4243004), (4536416, 4536608), (4536928, 4537248), (4537248, 4537440), (4537440, 4538736), (4517840, 4518320), (5744304, 5744400), (5748000, 5748128), (4331248, 4331328), (4362496, 4362672), (4522336, 4522512), (4541520, 4541760), (4497664, 4497744), (4506336, 4506960)]+[(0x454170,0x4541a5),(0x44a080,0x44a0d6),(0x454490,0x4548f2),(0x5aab20,0x5aaca0),(0x5a6e10,0x5a6e9b),(0x44d570,0x44d5d0),(0x5aaca0,0x5aacf2),(0x5aae80,0x5aaea0),(0x4556d0,0x45575e),(0x454240,0x454328),(0x454330,0x454412),(0x454420,0x454488),(0x5a6ea0,0x5a6ee2)]
for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
chain=m.get(CONTROLLER+0x3e4);neighbors=m.get(CONTROLLER+0x3e0)
actor.pose(40,20.,route_id=36);m.run(0x5a5340,esi=chain,budget=300000)
m.put(CONTROLLER+0x3c0,16);m.run(0x44cf30,(s['CTX'],CONTROLLER),budget=3000000)
limited=m.get(chain+0xb8,'i');assert limited>=0
cell=m.get(chain+8+4*limited);m.run(0x44d570,ecx=cell);intersection=m.reg('eax')
assert m.get(neighbors+4)==1 and m.get(ACTOR+0xa,'h')==50 and m.get(ACTOR+0x24)==4
action=0x11c0000;rows=[];queries=[];pending=[];admissions=[];admission_pending=[]
def observe(uc,pc,size,data):
 if admission_pending and pc==admission_pending[-1]:
  admission_pending.pop();admissions.append(m.reg("eax")&255)
 if pc==0x44efd0:admission_pending.append(m.get(m.reg("esp")))
 if pending and pc==pending[-1]['return']:
  row=pending.pop();row.update(target=m.reg('eax'),time=m.get(row['time_address']),distance=m.get(row['distance_address']));queries.append(row)
 if pc==0x44f470:
  sp=m.reg('esp');args=[m.get(sp+4*j)for j in range(1,6)]
  pending.append(dict(return_=m.get(sp),time_address=args[3],distance_address=args[4]));pending[-1]['return']=pending[-1].pop('return_')
m.uc.hook_add(UC_HOOK_CODE,observe)
def corpus(blocked):
 saved_context=bytes(m.uc.mem_read(CONTROLLER,0x500));saved_neighbor=bytes(m.uc.mem_read(neighbors,0x3a0));saved_intersection=bytes(m.uc.mem_read(intersection,0x170))
 entry=neighbors+8+0x38*m.get(neighbors+0x388,'b')
 for kind,selected,stopped,coordinate,absolute,swap in itertools.product((1,2,3,4),(-1,0,1),(0,1),(0.,10.,20.,50.,100.),(0.,.5),(False,True)):
  m.uc.mem_write(CONTROLLER,saved_context);m.uc.mem_write(neighbors,saved_neighbor);m.uc.mem_write(intersection,saved_intersection)
  if swap:
   for offset,size in((4,1),(8,2)):
    a=bytes(m.uc.mem_read(intersection+offset,size));b=bytes(m.uc.mem_read(intersection+offset+size,size))
    m.uc.mem_write(intersection+offset,b);m.uc.mem_write(intersection+offset+size,a)
  m.put(intersection,kind);m.put(intersection+12,selected&0xffffffff);m.put(ACTOR+0x3d4,absolute,'f');m.put(entry+0x10,coordinate,'f')
  m.run(0x454170,(bits(-1.),intersection,1),ecx=600,esi=action,budget=300000);m.put(action+0x204,stopped,'B')
  distance=bits(scalar(0x5a6e10,args=(chain,entry)));cap=bits(scalar(m.get(m.get(CONTROLLER)+0x5c),ecx=CONTROLLER))
  before=[kind,m.get(intersection+4,'b'),m.get(intersection+5,'b'),m.get(cell+0x5c,'b'),selected,stopped,distance,bits(absolute),cap]
  queries.clear();admissions.clear();m.run(0x454490,(s['CTX'],),ecx=action,budget=3000000)
  assert not pending and not admission_pending and len(queries)==1 and bool(queries[0]['target'])==blocked
  assert admissions==([1]if blocked else[]),'actual44EFD0 admission must be observed'
  q=queries[0];after=[*[m.get(action+0x1e4+4*k)for k in range(3)],m.get(action+0x204,'B'),m.get(intersection+12,'i'),*[m.get(ACTOR+0x890+4*k)for k in range(4)]]
  assert m.get(ACTOR+0x88c,'B')==1 and [m.get(action+at,'B')for at in(0x1e0,0x1f0,0x1f1,0x1f2)]==[1,0,1,0]
  assert m.get(action+0x20,'B')>0,'actual path producer must write output'
  rows.append([before,[int(blocked),admissions[0]if admissions else 0,q['time'],q['distance']],after])
corpus(False)
# The second shape uses actual registration; no query result is fabricated.
other=0x11a2000;m.uc.mem_write(other,bytes(m.uc.mem_read(ACTOR,0x900)))
m.put(other+0x4d0,1,'B');m.put(ACTOR+0x4d0,1,'B')
m.put(other+0xc,m.get(0x734588)+18*0xc90);m.put(ACTOR+0xc,m.get(0x734588)+18*0xc90)
m.put(other+0x14,0x1153000);m.put(0x1153004,1);m.put(other+0xa,2,'h');m.put(other+0x82c,0)
m.run(0x464860,(actor.ride+0x60,),eax=other+0x40,ecx=other+0x540,edx=other+0x828)
actor.pose(41,0.,update_world=False,actor=other,route_id=36)
m.put(0x1190000+0x10,0x11af000);m.run(0x57a1b0,(other,0));m.put(other+0x82c,m.reg('eax'))
m.run(0x57a380,esi=m.get(m.get(other+0x82c)+0x20))
m.run(0x44cf30,(s['CTX'],CONTROLLER),budget=3000000);assert m.get(neighbors+4)==2
corpus(True)
# Real offer/completion and mode predicate; preserve shared WORLD draw order.
world=m.get(0x7361f8);base=m.get(0x736158);count=m.get(0x73615c)
identity=(intersection-base)//0x170;assert 0<=identity<count
output_index,output_mode=0x11df000,0x11df004
seeds=(0,1,0x7fffffff,0x80000000,0xffffffff,0x13572468)
offers=[];completions=[]
for available,seed in itertools.product((False,True),seeds):
 m.put(chain+0xb8,limited if available else 0xffffffff);m.put(world+0x2d0,seed)
 m.run(0x4543d0,eax=s['CTX'],edi=output_index,esi=output_mode,budget=300000)
 pointer=m.get(output_index);index=(pointer-base)//0x170 if pointer else -1
 offers.append([int(available),seed,m.get(output_index),m.reg('eax'),index,m.get(output_mode),m.get(world+0x2d0)])
 for previous,mode,stopped in itertools.product((-1,identity,(identity+1)%count),(1,3),(0,1)):
  old_pointer=0 if previous==-1 else base+previous*0x170
  m.run(0x454170,(bits(-1.),old_pointer,mode),ecx=600,esi=action,budget=300000)
  m.put(action+0x204,stopped,'B');m.put(world+0x2d0,seed)
  m.run(0x454420,(s['CTX'],),ecx=action,budget=300000)
  pointer=m.get(action+0x200);current=(pointer-base)//0x170 if pointer else -1
  completions.append([int(available),identity,previous,mode,stopped,seed,m.reg('eax')&255,current,m.get(action+0x208),m.get(action+0x204,'B'),m.get(world+0x2d0)])
m.put(chain+0xb8,limited)
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual454490 controls on clear/blocked declared native scenes.\n'
text+='struct NativeTrafficStopCase {int kind,first_route,second_route,route,selected,stopped;unsigned distance,absolute,cap;unsigned found,admitted,time,gap;unsigned speed,duration,auxiliary;int stopped_after,selected_after;unsigned actor_override[4];};\n'
text+='static const NativeTrafficStopCase kNativeTrafficStop[]={\n'
for before,query,after in rows:text+=lit(before+query+after[:5]+[after[5:]])+',\n'
text+='};\n'
text+='struct NativeTrafficStopOffer {unsigned available,seed;int priority,index;unsigned mode,random;};\n'
text+='static const NativeTrafficStopOffer kNativeTrafficStopOffer[]={\n'
for row in offers:text+=lit([*row[:2],row[3]if row[3]<0x80000000 else row[3]-0x100000000,*row[4:]])+',\n'
text+='};\nstruct NativeTrafficStopComplete {unsigned available;int intersection,previous,mode,stopped;unsigned seed,complete;int current;unsigned current_mode,current_stopped,random;};\n'
text+='static const NativeTrafficStopComplete kNativeTrafficStopComplete[]={\n'+''.join(lit(row)+',\n'for row in completions)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/traffic_stop_action_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'actual traffic stop action fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,rows=rows,offers=offers,completions=completions),indent=2)+'\n',encoding='ascii')
print('PASS original traffic stop action controls',len(rows),'clear/blocked, both route slots; real offers/completion',len(offers),len(completions))
