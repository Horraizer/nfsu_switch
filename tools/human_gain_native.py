"""Actual human gain profile, camera-mode mix and control publication.

52C3F0,529500/529530,527080/527180,529EA0 and529CD0 execute. Actual525EA6
initializes the shift holder; parent/tuning/enabled references are inputs.
Actual52C3A0 copies the supplied player POV to sound2B0. No control, curve,
profile, category, camera or raw-gain result is hooked. Only allocation,
lock/free and final bank submission are supplied external IO. The raw-gain
category call is a terminal boundary, not a substituted result.

Four cars, sound levels0/3, changing physical inputs and four dt values are
declared. Divisor160, maximum10000, stock ratios, role1, camera mode0/2,
balance256/768 and fade1 are explicit conditional inputs, not a natural car
factory. Transient SND commands, optional196C producer, natural device/PCM
and complete module scheduling remain outside this stream comparison.
"""
import argparse,hashlib,struct,sys,json
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image,chunks
from physics256_native import Machine,ACTOR,DRIVER,STATUS,STOP,bits
from unicorn import UC_HOOK_CODE
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path);args=ap.parse_args()
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
rows=[];configs=[];tunings=[];states=set();notification_count=0;end_count=0
for car in ['CIVIC','RX7','SUPRA','INTEGRA']:
 for level in [0,3]:
  for initial in [1500.,8500.]:
   for camera in [0,2]:
    for phase in [256,768]:
     env,tuning,ring,ring2,event,settings,parent,manager,table=0x1120000,0x1110000,0x1130000,0x1130100,0x1144000,0x1150000,0x1160000,0x1170000,0x1180000
     m=Machine(Image(raw));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
     raw_stop=next(i.address for i in Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(m.uc.mem_read(0x529cd0,0x1d0)),0x529cd0) if i.mnemonic=='call' and '+ 0x38]' in i.op_str)
     data=records[car][0x9c0+level*100:0xa24+level*100];tunings.append(list(data))
     configs.append([level,*struct.unpack_from('<3h',data,0x18),bits(struct.unpack_from('<f',data,0x20)[0]),*struct.unpack_from('<4h',data,0x2c),bits(struct.unpack_from('<f',data,0x34)[0]),struct.unpack_from('<h',data,8)[0],*[bits(v) for v in struct.unpack_from('<4f',data,0x40)],struct.unpack_from('<h',data,0x50)[0],*[bits(v) for v in struct.unpack_from('<8f',records[car],0x2e0)],*struct.unpack_from('<2h',data,0xa),bits(initial),bits({'CIVIC':1./60.,'RX7':1./30.,'SUPRA':.01,'INTEGRA':.025}[car]),camera,phase])
     m.put(env,0x6beeb0);m.put(env+0x234,ACTOR);m.put(ACTOR+0x30,DRIVER);m.put(0x73619c,STATUS)
     m.put(env+0x1f0c,tuning);m.uc.mem_write(tuning,data)
     m.put(env+0x1760,event);m.put(env+0x1f80,160.,'f');m.put(env+0x1f24,10000.,'f')
     m.put(env+0x1f1c,initial,'f');m.put(env+0x1f20,0.,'f');m.put(env+0x1f8c,0.,'f');m.put(env+0x1f90,0.,'f')
     m.put(env+0x11c8,1.,'f');m.put(env+0x1d04,1,'B');m.put(ACTOR+0x14,0x11b0000);m.put(0x11b0004,1);m.put(env+0x1d54,min(max(level-2,0),1));m.put(env+0x1efc,level);m.put(env+0x18e8,0);m.put(env+0x18dc,0.,'f')
     ride=0x11a0000;m.put(ACTOR+0x20,ride)
     for k in range(8):m.put(ride+0x1d0+4*k,struct.unpack_from('<f',records[car],0x2e0+4*k)[0],'f')
     m.put(env+0x2ac,phase)
     m.put(settings+10*0x130+8,32767)
     m.run(0x525ea6,end=0x525ed4,ebp=env,ebx=0)
     m.put(env+0x1d24+4,env);m.put(env+0x1d24+8,tuning);m.put(env+0x1d24+0xc,1,'B')
     m.put(env+0x70,19437);m.put(env+0x6c,12345);m.put(ACTOR+0x18,0x1137000);m.put(0x1137024,camera,'b');m.put(0x113727c,0x1138000);m.put(0x113800c,1);m.run(0x52c3a0,ecx=env);assert m.get(env+0x2b0)==camera # Supplied non-interior output mode.
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
      m.put(env+0x1d18,gear)
      if gear!=previous_gear:m.run(0x5292b0 if gear>previous_gear else 0x5293e0,ecx=env)
      previous_gear=gear
      dt={'CIVIC':1./60.,'RX7':1./30.,'SUPRA':.01,'INTEGRA':.025}[car];m.put(env+0x244,dt,'f');m.put(env+0x240,m.get(env+0x240,'f')+m.get(env+0x244,'f'),'f')
      physical=1000. if 50<=tick<90 else 8800. if 180<=tick<200 else initial
      m.put(env+0x1d18,gear)
      m.put(ACTOR+0x3ec,80.,'f');m.put(ACTOR+0x3e0,physical,'f')
      m.put(DRIVER+0x1dc,0. if 120<=tick<210 else 1.,'f');m.put(DRIVER+0x1e0,1. if 120<=tick<210 else 0.,'f');m.put(DRIVER+0x1e4,0.,'f');m.put(STATUS,4)
      m.run(0x52c3f0,esi=env)
      m.run(0x529500,args=(bits(dt),),ecx=env)
      m.run(0x527080,args=(bits(dt),),ecx=env);m.run(0x527180,args=(bits(dt),),ecx=env)
      mode_before=m.get(env+0x18e8)
      m.run(0x529ea0,args=(bits(dt),),ecx=env)
      m.run(0x529cd0,args=(bits(dt),),end=raw_stop,ecx=env)
      main=[e for e in published if e[0]==0x529100];end=[e for e in published if e[0]==0x528f80]
      assert len(main)<=1 and len(end)<=1
      notification_count+=len(main);end_count+=len(end);state=m.get(env+0x1d14);states.add(state)
      rows.append([len(configs)-1,tick,bits(physical),gear,bits(dt),state,m.get(env+0x1f1c),m.get(env+0x1f8c),mode_before,m.get(env+0x18e8),m.get(env+0x18dc),m.get(env+0x18e4),m.get(env+0x238),m.get(env+0x23c),m.get(env+0x240),m.get(env+0x1f14),m.get(env+0x1f10,'B'),m.get(DRIVER+0x1e0),m.get(env+0x17dc),m.get(env+0x1d00),m.get(DRIVER+0x1dc)])
      published.clear()
assert states==set(range(7)),states
assert {r[8]for r in rows}=={0,1,2,3,5}
text='// Actual human input/shift/profile/raw gain, declared inputs and backend IO.\n'
text+='// Hash: shift,RPM,load,mode,blend,offset,rawA,rawB,time,lastOff,on.\n'
text+='struct NativeHumanGainCase {unsigned config[30];unsigned tuning[25];const char* publication;};\n'
text+='static constexpr NativeHumanGainCase kNativeHumanGain[]={\n'
for ci,config in enumerate(configs):
 selected=[r for r in rows if r[0]==ci];assert len(selected)==320 and [r[1]for r in selected]==list(range(320))
 digest=hashlib.sha256()
 for r in selected:digest.update(struct.pack('<11I',*[r[k]for k in [5,6,7,9,10,11,12,13,14,15,16]]))
 words=struct.unpack('<25I',bytes(tunings[ci]))
 text+=' {{'+','.join('0x%08XU'%(v&0xffffffff)for v in config)+'},{'+','.join('0x%08XU'%v for v in words)+'},"'+digest.hexdigest().upper()+'"},\n'
text+='};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/human_gain_native.inc'
if args.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original human gain stream fixture differs'
if args.out:
 relative=args.out.resolve().relative_to(Path.cwd());assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
 args.out.mkdir(exist_ok=False)
 (args.out/'oracle.json').write_text(json.dumps(dict(rows=rows,configs=configs,tunings=tunings,states=sorted(states),boundary=__doc__),indent=2)+'\n',encoding='ascii')
print('PASS actual conditional human gain/control streams, cases='+str(len(configs)))
