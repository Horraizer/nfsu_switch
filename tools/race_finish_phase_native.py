"""Original three-clock chain from supplied driving frames through first table.

Runs full delta min/max/quantization585C50, unscaled phase585D70, world585E00,
FEng budget4F5CF0 and actual loading gate422FE9. The transition from driving
is a supplied state boundary. No whole game-loop ordering, earned drive or
live resource-completion time is claimed. Readiness callbacks are explicit.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_FPCW
from world_body_trace import Image
from physics256_native import Machine,ACTOR,STACK
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--write-fixture',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
w=Path(__file__).resolve().parents[1];raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
for scenario,(first,second,driving,ready_after) in enumerate(((16666,16666,17,0),(16667,16667,18,0),
       (20200,20170,17,0),(20000,33333,23,170),(10000,40000,14,230),(21667,21333,17,0))):
 for cw in (0x007F,0x037F):
  m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,cw)
  m.allowed=[(0x585C50,0x585D69),(0x585D70,0x585DF6),(0x585E00,0x585EBA),
             (0x4F5CF0,0x4F5D02),(0x422FE9,0x42302A),(0x674898,0x67490D)]
  for at in (0x734570,0x7345A0,0x73456C,0x6F0888,0x73AD3C,0x734580,0x6F08A0,0x6F08A4):m.put(at,0)
  m.put(0x6F0880,1);m.put(0x6F0894,3,'f');view=0x1108000;m.put(view+0x14,8)
  busy=[False];advanced=[]
  m.hook(0x445100,'resource_busy',pop=4,callback=lambda q:(q.ret(4,int(busy[0]))))
  m.hook(0x4DE130,'ending_notification',value=0)
  m.hook(0x421AE0,'request_first_table',callback=lambda q:(advanced.append(True),q.ret(4)))
  samples=[];ui=0;phase_origin=0;pre_world=0
  for frame in range(1,driving+302):
   us=second if frame%2==0 else first
   m.put(STACK+0x28,us/1000000,'f');m.run(0x585C50,end=0x585D69,edi=ACTOR)
   delta=m.get(0x73AD38,'f');m.run(0x585D70);m.run(0x4F5CF0,end=0x4F5D02)
   ui_budget=m.reg('eax')
   m.put(0x73457C,delta if frame<=driving else 0,'f');m.run(0x585E00)
   if frame<=driving:
    ui+=ui_budget;pre_world=m.get(0x6F08A0);continue
   loading_frame=frame-driving-1
   if not loading_frame:phase_origin=m.get(0x73AD3C);m.put(view+4,phase_origin)
   busy[0]=loading_frame<ready_after;advanced.clear();m.run(0x422FE9,end=0x42302A,esi=view)
   phase=m.get(0x73AD3C)-phase_origin
   samples.append(dict(frame=loading_frame,phase=phase,residue_bits=m.get(0x734570),
                       world=m.get(0x6F08A0),resources=not busy[0],ready=bool(advanced)))
   assert m.get(0x6F08A0)==pre_world
   if advanced:break
  else:raise AssertionError('original phase gate never reached first table')
  rows.append(dict(scenario=scenario,first=first,second=second,driving=driving,ready_after=ready_after,cw=cw,
                   pre_ui=ui,pre_world=pre_world,samples=samples,hooks=m.events))
fixture='// Original full producer,three clocks and loading-readiness consumer.\n'
fixture+='struct NativeFinishPhaseCase {unsigned first,second,driving,ready_after,pre_ui,pre_world;};\n'
fixture+='static constexpr NativeFinishPhaseCase kNativeFinishPhaseCases[]={\n'
for r in rows:
 if r['cw']==0x007F:fixture+='    {%d,%d,%d,%d,%d,%d},\n'%(r['first'],r['second'],r['driving'],r['ready_after'],r['pre_ui'],r['pre_world'])
fixture+='};\nstruct NativeFinishPhaseSample {unsigned scenario,frame,phase,residue_bits,world;bool resources,ready;};\n'
fixture+='static constexpr NativeFinishPhaseSample kNativeFinishPhaseSamples[]={\n'
for r in rows:
 if r['cw']!=0x007F:continue
 for s in r['samples']:fixture+='    {%d,%d,%d,0x%08XU,%d,%s,%s},\n'%(r['scenario'],s['frame'],s['phase'],s['residue_bits'],s['world'],str(s['resources']).lower(),str(s['ready']).lower())
fixture+='};\n';target=w/'tests/race_finish_phase_native.inc'
if a.write_fixture:target.write_bytes(fixture.encode('ascii'))
else:assert target.read_text(encoding='ascii')==fixture,'three-clock finish fixture stale'
(a.out/'oracle.json').write_bytes((json.dumps(dict(sha256=hashlib.sha256(raw).hexdigest(),scope=__doc__,rows=rows),indent=2,ensure_ascii=True)+'\n').encode('ascii'))
print('PASS original three-clock chain schedules=%d, both control words, odd frames and late readiness'%len(rows))
