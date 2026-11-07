from pathlib import Path
import sys,struct,json,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine,bits
from unicorn import UC_HOOK_CODE
import argparse
ap=argparse.ArgumentParser(description='Actual human delayed shift cue and RPM gain stages; inactive output actor')
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
root=args.retail_root
raw=(root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bun=(root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
records={}
for tag,first,last in chunks(bun):
 if tag==0x34600:
  for at in range((first+8+15)&~15,last,0xc90):
   record=bun[at:at+0xc90]
   if len(record)==0xc90:records[record[:16].split(b'\0')[0].decode('ascii')]=record
im=Image(raw);rows=[];configs=[]
for car in ['CIVIC','RX7','SUPRA','INTEGRA']:
 for level in [0,3]:
  data=records[car][0x9c0+100*level:0xa24+100*level];ci=len(configs);configs.append([bits(struct.unpack_from('<f',data,0x24)[0]),bits(struct.unpack_from('<f',data,0x38)[0])])
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
  env,tuning,ring=0x1120000,0x1110000,0x1130000;m.uc.mem_write(tuning,data);m.put(env+0x1f0c,tuning);m.put(env+0x1d28,env)
  m.put(env+0x1f28+5,10,'B');m.put(env+0x1f28+0x10,ring);m.put(0x6f1dd8,0)
  fired=[]
  def observe(uc,address,size,user):
   if address==0x529100:fired.append(m.get(m.reg('esp')+8))
  m.uc.hook_add(UC_HOOK_CODE,observe)
  for tick in range(120):
   action,gear,rpm={5:(1,3,7000.),6:(1,4,2000.),20:(0,3,4000.),21:(0,1,5000.),25:(1,2,2999.),27:(1,3,3000.),28:(1,4,9000.),40:(0,2,2000.)}.get(tick,(-1,2,4000.))
   dt=[1/60,1/30,.01,.025][ci%4];old=m.get(env+0x240,'f')
   m.put(env+0x1d18,gear);m.put(env+0x1f1c,rpm,'f')
   if action>=0:m.run(0x5292b0 if action else 0x5293e0,ecx=env)
   m.put(env+0x240,old+struct.unpack('<f',struct.pack('<f',dt))[0],'f')
   fired.clear();m.run(0x529530,end=0x52957a,ecx=env)
   assert len(fired)<=1
   rows.append([ci,tick,action&0xffffffff,gear,bits(rpm),bits(old),bits(dt),fired[0] if fired else 0,m.get(env+0x1d48,'B'),m.get(env+0x1d4c),m.get(env+0x1d50)])
  assert not m.events
text='// Actual5292B0/5293E0 and529530 cue stage; inactive output actor, no result hooks.\n'
for name,data in [('kNativeHumanShiftCueConfig',configs),('kNativeHumanShiftCue',rows)]:
 text+='static constexpr unsigned '+name+'[]['+str(len(data[0]))+']={\n'+''.join(' {'+','.join('0x%08XU'%x for x in r)+'},\n' for r in data)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/human_shift_feedback_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text(encoding='ascii')==text,'native delayed shift cue fixture differs'
print('PASS actual pending shift cue stage',len(rows),'rows; bank output explicitly inactive')
print('Delay configurations',configs)

im=Image(raw);rows=[]
for car in ['CIVIC','RX7','SUPRA','INTEGRA']:
 for level in [0,3]:
  data=records[car][0x9c0+100*level:0xa24+100*level]
  gain=struct.unpack_from('<h',data,8)[0]
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
  env,tuning,event=0x1120000,0x1110000,0x1140000;m.uc.mem_write(tuning,data)
  m.put(event+4,env);m.put(event+8,tuning);m.put(event+0xc,1,'B');m.put(0x6f1dd8,0)
  for rpm in [-100.,0.,1499.9,1500.,1500.1,2999.9,3000.,6500.,6999.9,7000.,7999.9,8000.,10000.]:
   m.put(env+0x1f1c,rpm,'f');m.run(0x529100,(event,1,0,4096,0),end=0x5291e5)
   actual=m.get(m.reg('esp')+0x20,'i');rows.append([gain,bits(rpm),actual&0xffffffff])
  assert not m.events
text='// Actual529100 through native integer gain lookup, before category/IO.\nstatic constexpr unsigned kNativeHumanShiftGain[][3]={\n'+''.join(' {'+','.join('0x%08XU'%x for x in r)+'},\n' for r in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/human_shift_gain_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text(encoding='ascii')==text,'native shift gain fixture differs'
print('PASS native shift gain controls',len(rows),'rows')
