"""Original human Turbo controls and selection on supplied audio actor state.

Existing primary event, zero-initialized state and PCM completion are named
inputs. Only output allocation/submission is hooked. This is not a natural
race, device/PCM or original allocator-lifetime equivalence claim.
"""
from pathlib import Path
import sys,json,struct,hashlib
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image,chunks
from audio_position_native import prepared
import argparse
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path);args=ap.parse_args();root=args.retail_root;raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bun=(root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9';records={}
for tag,first,last in chunks(bun):
 if tag==0x34600:
  for at in range((first+8+15)&~15,last,0xc90):records[bun[at:at+16].split(b'\0')[0].decode('ascii')]=bun[at:at+0xc90]

from physics256_native import bits,STACK,Machine
rows=[];inputs=[];cases=[]
for car,level in [('INTEGRA',1),('CIVIC',3),('SKYLINE',3),('240SX',0)]:
 data=records[car][0x9c0+100*level:0xa24+100*level];spool_gain,release_gain=struct.unpack_from('<h',data,0)[0],struct.unpack_from('<h',data,4)[0];capacity=struct.unpack_from('<f',data,16)[0]
 m,pos=prepared(Image(raw),0x6f42d0);obj=pos-0x20;env,actor,tuning,event,settings=0x1120000,0x1123000,0x1110000,0x1130000,0x1140000
 m.run(0x5364e0,(settings,));m.put(0x7356ac,settings);m.put(0x6f1dd8,1);m.put(0x6f227c,0)
 m.put(obj,0x6bee5c);m.put(pos,0x6bee30);m.put(obj+4,env);m.put(obj+8,tuning);m.put(obj+0xc,1,'B');m.put(obj+0x234,event)
 m.put(env,0x6beeb0);m.put(env+0x1274,obj);m.put(env+0x12b0,1,'B');m.put(env+0x234,actor);m.uc.mem_write(tuning,data);m.put(env+0x2ac,512);m.put(env+0x6c,32768)
 # Execute original base ring initialization, then its explicit zero fill.
 m.run(0x525d45,end=0x525d8b,ebp=env,ebx=0);m.run(0x584310,(0,),eax=env+0x127c)
 assert m.get(env+0x1281,'B')==3 and m.get(env+0x1282,'B')==3
 # Supplied already allocated primary event; its first creator parameters are
 # separately observed in525AA0. This comparison starts at the next update.
 m.put(event+24,7000);m.put(env+0x240,1.,'f')
 allocated=[]
 def allocation(cpu):
  at=0x1131000+len(allocated)*0x100;allocated.append(at);cpu.ret(value=at)
 m.hook(0x5256f0,'output_event_allocate_28_bytes',callback=allocation)
 m.hook(0x64c1a0,'output_event_submit_no_device',value=0)
 category=m.get(settings+0x398);position=m.get(obj+0x70)
 inputs.append([1,spool_gain,release_gain,capacity,0.,7000.,0.,1/60,32768,512,category,position,0,0])
 rows.append(None);first=len(rows)
 for tick in range(420):
  pressure=1.3 if tick%210<115 else 0. if tick%210<160 else .2
  pressure=struct.unpack('<f',struct.pack('<f',pressure))[0]
  rpm=7000. if tick<220 else 1050. if tick<260 else 5000.
  dt=1/60;complete=int(tick%210==155)
  if complete:m.put(obj+0x238,0) # Named PCM completion input, not natural device proof.
  previous=m.get(env+0x1278,'f')
  m.run(0x584270,(bits(previous-pressure),),eax=env+0x127c);m.put(env+0x1278,pressure,'f')
  m.put(env+0x1f1c,rpm,'f');m.put(env+0x1f8c,100.,'f')
  count=len(allocated);m.run(0x528b60,(bits(dt),),ecx=env)
  triggered=len(allocated)>count
  secondary=[m.get(allocated[-1]+4+4*i) for i in range(6)] if triggered else [0]*6
  rows.append([m.get(obj+0x2c0,'f'),m.get(obj+0x2c4,'f'),m.get(env+0x1288,'f'),m.get(env+0x12a4),m.get(0x6f227c),0,int(triggered)]+[m.get(event+4+4*i) for i in range(6)]+secondary)
  inputs.append([0,spool_gain,release_gain,capacity,pressure,rpm,100.,dt,32768,512,category,position,0,complete])
 cases.append(dict(car=car,level=level,rows=[first,len(rows)],hooks=m.events,completion='supplied missing event at ticks155,365',primary='supplied existing zero-event with initial RPM7000'))
selections=[]
for variant in [-1,0,1,2,3,100]:
 for level in range(4):
  m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)]
  owner,config,manager=0x1100000,0x1110000,0x1120000
  if variant==-1:
   m.run(0x57d160,(0,0,0),eax=4,edx=config+0x10,ecx=0)
   assert m.get(config+0x435,'b')==-1
  else:m.put(config+0x435,variant,'b')
  m.put(STACK+0x18,level);m.put(owner+0x78,0)
  m.run(0x51e2f3,end=0x51e365,ebp=owner,edi=config,ebx=0,edx=0xffffffff)
  record=[m.get(owner+0x1e8+4*i) for i in range(3)]
  m.put(0x6f1dd8,1)
  m.run(0x522020,(record[1],0,1,9),end=0x521e0e,esi=manager,ecx=record[0],edx=record[2])
  strings=[]
  for off in [0x30,0x2c]:strings.append(bytes(m.uc.mem_read(m.get(manager+off),80)).split(b'\0')[0].decode('ascii'))
  assert not m.events;selections.append(dict(variant=variant,level=level,record=record,directory=strings[0],bank=strings[1]))

def word(value):return struct.unpack('<I',struct.pack('<f',value))[0]
fixture='// Original525D45,584310,584270,528B60,528830,528470 and525AA0.\n'
fixture+='// Supplied existing primary event; explicit allocator/device/PCM completion boundaries.\n'
fixture+='static constexpr unsigned kNativeTurboControl[][33]={\n'
for input_row,output_row in zip(inputs,rows):
 encoded=[word(v) if i in [3,4,5,6,7] else int(v)&0xffffffff for i,v in enumerate(input_row)]
 encoded+=([0]*19 if output_row is None else [word(v) if i<3 else int(v)&0xffffffff for i,v in enumerate(output_row)])
 fixture+='    {'+','.join(map(str,encoded))+'},\n'
fixture+='};\nstruct NativeTurboSelection {int variant,level;const char* bank;};\n'
fixture+='static constexpr NativeTurboSelection kNativeTurboSelection[]={\n'
for row in selections:
 fixture+='    {'+str(row['variant'])+','+str(row['level'])+',"Sound/TURBO/'+row['bank']+'.abk"},\n'
fixture+='};\n'
fixture=fixture.replace('\\n','\n')
path=Path(__file__).resolve().parents[1]/'tests/turbo_audio_native.inc'
if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
else:assert path.read_text(encoding='ascii')==fixture,'native turbo controls or selection fixture differs'
if args.out:
 args.out.mkdir(exist_ok=False);(args.out/'oracle.json').write_text(json.dumps(dict(cases=cases,selections=selections,rows=rows,scope=__doc__),indent=2)+'\n',encoding='ascii')
print('PASS native turbo parent, spool, release parameters, RNG and bank resolution; explicit output completion/allocator/device boundaries')
