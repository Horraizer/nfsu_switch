"""Actual remote shift notifications and control updates; device IO declared.

Only allocation/lock/free and final bank submission are external inputs. Real
523990/523A70/523E30,529100/528F80, constructors and526AA0/536C20 execute.
Positional gain19437, pan12345, non-interior mode, category1/5 gains18000/23000
and gain profile state2 are supplied. Natural module/device scheduling,
other adaptive limiter transitions and PCM are outside this control witness.
"""
import argparse,hashlib,struct,sys,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image,chunks
from physics256_native import Machine,ACTOR,DRIVER,STATUS,STOP,bits
from unicorn import UC_HOOK_CODE
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bun=(args.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
records={}
for tag,first,last in chunks(bun):
 if tag==0x34600:
  for at in range((first+8+15)&~15,last,0xc90):
   rec=bun[at:at+0xc90]
   if len(rec)==0xc90:records[rec[:16].split(b'\0')[0].decode('ascii')]=rec
rows=[];configs=[];states=set();notification_count=0;end_count=0
for level in [0,3]:
 for initial in [1500.,6999.99,7000.,8500.]:
  env,tuning,ring,ring2,event,settings,parent,manager,table=0x1120000,0x1110000,0x1130000,0x1130100,0x1144000,0x1150000,0x1160000,0x1170000,0x1180000
  m=Machine(Image(raw));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
  data=records['CIVIC'][0x9c0+level*100:0xa24+level*100]
  configs.append([level,*struct.unpack_from('<3h',data,0x18),bits(struct.unpack_from('<f',data,0x20)[0]),*struct.unpack_from('<4h',data,0x2c),bits(struct.unpack_from('<f',data,0x34)[0]),struct.unpack_from('<h',data,8)[0]])
  m.put(env,0x6bf2c0);m.put(env+0x234,ACTOR);m.put(ACTOR+0x30,DRIVER);m.put(0x73619c,STATUS)
  m.put(env+0x1f0c,tuning);m.uc.mem_write(tuning,data)
  m.put(env+0x1760,event);m.put(env+0x1f80,160.,'f');m.put(env+0x1f24,10000.,'f')
  m.put(env+0x1f1c,0.,'f');m.put(env+0x1f20,0.,'f');m.put(env+0x1f8c,0.,'f');m.put(env+0x1f90,0.,'f')
  m.put(env+0x1efc,level);m.put(env+0x18e8,2);m.put(env+0x18dc,1.,'f')
  m.put(env+0x1d24+4,env);m.put(env+0x1d24+8,tuning);m.put(env+0x1d24+0xc,1,'B')
  m.put(env+0x70,19437);m.put(env+0x6c,12345);m.put(env+0x2b0,0) # Supplied non-interior output mode.
  m.put(0x6f1dd8,1);m.put(0x7356ac,settings);m.put(0x7356a8,parent)
  m.put(settings+0x130+8,18000);m.put(settings+5*0x130+8,23000)
  for at,size,storage in [(env+0x1f28,10,ring),(env+0x1f50,5,ring2)]:
   m.put(at,0x6b9694);m.put(at+5,size,'B');m.put(at+0x10,storage)
  m.put(manager,table);m.put(table,STOP+0x220);m.put(table+4,STOP+0x224)
  allocated=[0x1190000];published=[];kind=[0]
  def context(x):x.put(x.get(x.reg('esp')+4),manager);x.ret()
  def allocate(x):
   assert x.get(x.reg('esp')+4)==0x1c
   at=allocated[0];allocated[0]+=0x100;x.ret(12,at)
  def submit(x):
   sp=x.reg('esp');assert x.get(sp+4)==0x73d804
   published.append([kind[0]]+[x.get(x.get(sp+8)+i*4) for i in range(6)]);x.ret(value=0)
  def observe(uc,address,size,data):
   if address in (0x529100,0x528f80):kind[0]=address
  m.uc.hook_add(UC_HOOK_CODE,observe)
  m.hook(0x64b510,'device-allocation-context',callback=context)
  m.hook(0x64b580,'device-allocation-lock');m.hook(0x64b5a0,'device-allocation-unlock')
  m.hook(STOP+0x220,'device-output-allocation',callback=allocate)
  m.hook(STOP+0x224,'device-output-free',pop=4)
  m.hook(0x64c1a0,'device-output-submission',callback=submit)
  previous_gear=2
  for tick in range(320):
   gear=2 if tick<6 else 3 if tick<10 else 4 if tick<100 else 3 if tick<240 else 4
   if gear!=previous_gear:m.run(0x523990 if gear>previous_gear else 0x523a70,ecx=env)
   previous_gear=gear
   dt=1./60.;m.put(env+0x244,dt,'f');m.put(env+0x240,tick*dt,'f')
   m.run(0x523b30,args=(bits(dt),),ecx=env);m.run(0x527180,args=(bits(dt),),ecx=env)
   m.put(ACTOR+0x3ec,80.,'f');m.put(ACTOR+0x3e0,initial,'f')
   m.put(DRIVER+0x1dc,1.,'f');m.put(DRIVER+0x1e0,0.,'f');m.put(DRIVER+0x1e4,0.,'f');m.put(STATUS,4)
   m.run(0x523c30,ecx=env);m.run(0x523e30,args=(bits(dt),),ecx=env);m.run(0x523c30,ecx=env)
   main=[e for e in published if e[0]==0x529100];end=[e for e in published if e[0]==0x528f80]
   assert len(main)<=1 and len(end)<=1
   for e in main:assert e[1]==1 and e[3:]==[4096,12345,0,0],e
   for e in end:assert e[1]==0 and e[3:]==[4096,0,0,0],e
   notification_count+=len(main);end_count+=len(end);state=m.get(env+0x1d14);states.add(state)
   rows.append([len(configs)-1,tick,bits(initial),gear,bits(dt),state,m.get(env+0x1f1c),m.get(env+0x1f8c),int(bool(main)),main[0][2] if main else 0,12345,int(bool(end)),end[0][2] if end else 0])
   published.clear()
assert states==set(range(7)),states
assert notification_count==24 and end_count==8,(notification_count,end_count)
text='// Actual523990/523A70/523E30 and shift constructors/category arithmetic.\n'
text+='// Supplied positional inputs, non-interior mode, gain profile state2.\n'
for name,values,width in [('kNativeRemoteShiftConfig',configs,11),('kNativeRemoteShift',rows,13)]:
 text+='static constexpr unsigned '+name+'[]['+str(width)+']={\n'
 text+=''.join(' {'+','.join('0x%08XU'%(v&0xffffffff) for v in row)+'},\n' for row in values)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/remote_shift_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original remote shift fixture differs'
print('PASS actual remote shift dispatch/controls, all7 states, rows='+str(len(rows)))
