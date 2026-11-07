"""Actual remote524130 Turbo controls on declared actor and output states.

Runs523C30 for the retained pressure latch,524130/528830/528470 for output,
and52EA00 with the real6F42D0 profile for position. Primary event and secondary
completion are supplied; only output allocation/submission is replaced.
This is not natural actor/device scheduling or full PCM equivalence.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,itertools
sys.dont_write_bytecode=True
from world_body_trace import Image,chunks
from audio_position_native import prepared
from physics256_native import bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
a=ap.parse_args();raw=(a.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bun=(a.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
image=Image(raw);records={};rows=[];positions=[]
for tag,first,last in chunks(bun):
 if tag==0x34600:
  for at in range((first+8+15)&~15,last,0xc90):
   records[bun[at:at+16].split(b'\0')[0].decode('ascii')]=bun[at:at+0xc90]
for car,level in [('INTEGRA',1),('CIVIC',3),('SKYLINE',2),('240SX',0)]:
 data=records[car][0x9c0+100*level:0xa24+100*level]
 m,pos=prepared(image,0x6f42d0);obj=pos-0x20
 env,actor,tuning,event,settings,driver,status=(0x1120000,0x1123000,0x1110000,0x1130000,0x1140000,0x1150000,0x1160000)
 m.run(0x5364e0,(settings,));m.put(0x7356ac,settings);m.put(0x6f1dd8,1);m.put(0x6f227c,0)
 m.put(obj,0x6bee5c);m.put(pos,0x6bee30);m.put(obj+4,env);m.put(obj+8,tuning);m.put(obj+0xc,1,'B');m.put(obj+0x234,event)
 m.put(env,0x6bf2c0);m.put(env+0x1274,obj);m.put(env+0x12b0,1,'B');m.put(env+0x234,actor);m.uc.mem_write(tuning,data)
 m.put(env+0x2ac,256);m.put(env+0x6c,12345);m.put(event+24,7000)
 m.put(actor+0x30,driver);m.put(env+0x1f80,160.,'f');m.put(env+0x1f24,10000.,'f');m.put(env+0x244,1/60,'f')
 m.put(status,4);m.put(0x73619c,status)
 allocated=[]
 def allocation(cpu):
  at=0x1131000+len(allocated)*0x100;allocated.append(at);cpu.ret(value=at)
 m.hook(0x5256f0,'output_event_allocate_28_bytes',callback=allocation)
 m.hook(0x64c1a0,'output_event_submit_no_device',value=0)
 for tick in range(321):
  # First row conditions match a supplied already-created zero-charge primary.
  load=0. if tick==0 or tick%160>=105 else 20. if tick%160 in (1,104) else 100.
  on=int(tick%160<105 and tick!=0);rpm=7000. if tick<220 else 1050. if tick<260 else 5000.
  complete=int(tick%160==155)
  if complete:m.put(obj+0x238,0)
  m.put(env+0x1f10,on,'B');m.put(env+0x1fa4,1.,'f');m.put(env+0x1fa8,1.,'f')
  m.put(actor+0x3e0,rpm,'f');m.put(actor+0x3ec,80.,'f')
  m.run(0x523c30,ecx=env)
  pressure=m.get(env+0x1278,'f');assert bits(pressure)==(0x3fb0a3d7 if on else 0)
  m.put(env+0x1f1c,rpm,'f');m.put(env+0x1f8c,load,'f')
  count=len(allocated);m.run(0x524130,(bits(1/60),),ecx=env)
  triggered=len(allocated)>count
  secondary=[m.get(allocated[-1]+4+4*i)for i in range(6)]if triggered else [0]*6
  inputs=[int(tick==0),struct.unpack_from('<h',data,0)[0]&0xffffffff,struct.unpack_from('<h',data,4)[0]&0xffffffff,struct.unpack_from('<I',data,16)[0],bits(pressure),bits(rpm),bits(load),bits(1/60),12345,256,m.get(settings+0x398),m.get(obj+0x70),complete]
  outputs=[m.get(obj+0x2c0),m.get(obj+0x2c4),m.get(env+0x12a4),m.get(0x6f227c),int(triggered)]+[m.get(event+4+4*i)for i in range(6)]+secondary
  rows.append(inputs+outputs)
 assert all(e['hook']in('output_event_allocate_28_bytes','output_event_submit_no_device') for e in m.events),m.events
# Each position starts from the real fresh preferences and profile.
for source,eye,forward in itertools.product(((0.,0.,0.),(1.,0.,0.),(10.,0.,0.),(0.,14.9,0.),(0.,25.,0.),(24.,0.,3.),(0.,-12.,0.),(50.,20.,0.)),((6.,0.,0.),(0.,0.,0.),(2.,3.,4.)),((1.,0.,0.),(0.,1.,0.),(.6,.8,0.))):
 m,obj=prepared(image,0x6f42d0)
 for k in range(3):m.put(0x1108000+4*k,source[k],'f');m.put(0x1104000+0x40+4*k,eye[k],'f');m.put(0x1104000+0x50+4*k,forward[k],'f')
 m.run(0x52ea00,(0x1108000,0x1108100),ecx=obj);assert not m.events
 positions.append([*[bits(v)for v in (*source,*eye,*forward)],m.get(obj+0x50),m.get(obj+0x4c)])
text='// Actual remote524130 and fresh6F42D0 spatial publication; declared IO boundary.\n'
text+='static constexpr unsigned kNativeRemoteTurbo[][30]={\n'+''.join(' {'+','.join(str(v)+'U'for v in row)+'},\n'for row in rows)+'};\n'
text+='static constexpr unsigned kNativeTurboPosition[][11]={\n'+''.join(' {'+','.join(str(v)+'U'for v in row)+'},\n'for row in positions)+'};\n'
assert text.isascii();p=Path(__file__).resolve().parents[1]/'tests/remote_turbo_native.inc'
if a.write_fixture:p.write_bytes(text.encode('ascii'))
else:assert p.read_text('ascii')==text,'native remote Turbo fixture differs'
if a.out:
 assert a.out.resolve().relative_to(Path.cwd()).parts[0].startswith('build');assert not a.out.exists()
 a.out.write_text(json.dumps(dict(scope=__doc__,rows=rows,positions=positions),indent=2)+'\n',encoding='ascii')
print('PASS actual remote Turbo controls='+str(len(rows))+' and spatial='+str(len(positions)))
