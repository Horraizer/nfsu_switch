"""Execute PC frame-delta producer,60Hz world counter and strict finish consumer.

Supplied measured intervals/blank counts and finish-view records are seams.
Full producer and post-clamp suffix are identified separately. No live clock
or explanation of video cadence is inferred from this numeric witness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from unicorn.x86_const import UC_X86_REG_FPCW
from world_body_trace import Image
from physics256_native import Machine,ACTOR,STACK

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--write-fixture',action='store_true')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
w=Path(__file__).resolve().parents[1]
b=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(b)

def run(first,second,count,mode,blanks,full,cw):
 producer,consumer=Machine(image),Machine(image)
 for m in (producer,consumer):m.uc.reg_write(UC_X86_REG_FPCW,cw)
 producer.allowed=[(0x585C50,0x585D69),(0x674898,0x67490D)]
 consumer.allowed=[(0x4F5CF0,0x4F5D02),(0x585E00,0x585EBA),(0x674898,0x67490D)]
 for at in (0x734570,0x7345A0):producer.put(at,0)
 for at in (0x734580,0x6F08A0,0x6F08A4):consumer.put(at,0)
 producer.put(0x6F0880,mode);producer.put(0x6F0894,float(blanks),'f')
 ages=[];ui=0
 for n in range(count):
  measured=(second if n&1 else first)/1000000
  producer.put(STACK+0x28,measured,'f')
  if full:producer.run(0x585C50,end=0x585D69,edi=ACTOR)
  else:
   producer.run(0x585C54,end=0x585C64)
   producer.run(0x585CD5,end=0x585D69)
  seconds=producer.get(0x73AD38,'f');consumer.put(0x73AD38,seconds,'f')
  consumer.run(0x4F5CF0,end=0x4F5D02);budget=consumer.reg('eax');ui+=budget
  consumer.put(0x73457C,seconds,'f');consumer.run(0x585E00)
  ages.append(dict(seconds=seconds,ui=budget,world_frames=consumer.get(0x6F08A0),calls=consumer.get(0x6F08A4)))
 assert not producer.events and not consumer.events
 return dict(first=first,second=second,count=count,mode=mode,blanks=blanks,full=full,cw=cw,
             ui=ui,world_frames=consumer.get(0x6F08A0),producer_residue=producer.get(0x734570,'f'),
             world_residue=consumer.get(0x734580,'f'),frames=ages)

rows=[]
for full in (False,True):
 for mode,blanks in ((1,1),(1,3),(0,3)) if full else ((1,3),):
  for first,second,count in ((16666,16666,30),(16667,16667,30),(16666,16667,30),
                             (20000,33333,181),(10000,40000,360),(21333,21667,181)):
   for cw in (0x007F,0x037F):rows.append(run(first,second,count,mode,blanks,full,cw))
for row in rows:
 if row['full'] and row['mode']==1 and row['blanks']==1 and row['first'] in (16666,16667):assert row['ui']==600
 if not row['full'] and row['first'] in (16666,16667):assert row['ui']==590

edges=[]
for delay in (180,360):
 for initial in (0,1,12345):
  for offset in (delay-1,delay,delay+1):
   m=Machine(image);m.allowed=[(0x423645,0x42365D)]
   m.put(ACTOR+0x58,initial);m.put(ACTOR+0x5C,delay);m.put(0x6F08A0,initial+offset)
   calls=[];m.hook(0x423F20,'request_ending_transition',callback=lambda q:(calls.append(True),q.ret()))
   m.run(0x423645,end=0x42365D,eax=ACTOR,esi=ACTOR+0x1000)
   assert bool(calls)==(offset>delay)
   edges.append(dict(initial=initial,delay=delay,offset=offset,ready=bool(calls)))

fixture='// Original585C50..585D69,585E00 and423645 strict consumer.\n'
fixture+='struct NativeFinishClock {unsigned first,second,count,mode,blanks;bool full;unsigned ui,world_frames;};\n'
fixture+='static constexpr NativeFinishClock kNativeFinishClock[]={\n'
for r in rows:
 if r['cw']!=0x007F:continue
 fixture+='    {%d,%d,%d,%d,%d,%s,%d,%d},\n'%(r['first'],r['second'],r['count'],r['mode'],r['blanks'],str(r['full']).lower(),r['ui'],r['world_frames'])
fixture+='};\nstruct NativeFinishClockEdge {unsigned initial,delay,offset;bool ready;};\n'
fixture+='static constexpr NativeFinishClockEdge kNativeFinishClockEdges[]={\n'
for r in edges:fixture+='    {%d,%d,%d,%s},\n'%(r['initial'],r['delay'],r['offset'],str(r['ready']).lower())
fixture+='};\n'
target=w/'tests/race_finish_clock_native.inc'
if a.write_fixture:target.write_bytes(fixture.encode('ascii'))
else:assert target.read_text(encoding='ascii')==fixture,'finish clock fixture stale'
(a.out/'oracle.json').write_bytes((json.dumps(dict(rows=rows,edges=edges,scope=__doc__,sha256=hashlib.sha256(b).hexdigest()),indent=2,ensure_ascii=True)+'\n').encode('ascii'))
print('PASS original full/post-clamp producer schedules=%d and strict finish edges=%d'%(len(rows),len(edges)))
