"""Original477300 accumulator, real588AB0, explicit rest/refresh boundaries."""
from pathlib import Path
import hashlib,json,struct,sys
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--retail-root",type=Path,required=True)
parser.add_argument("--out",type=Path,required=True)
args=parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
from physics256_native import Machine,STOP
from world_body_trace import Image
retail=args.retail_root
raw=(retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for speed,mask,rest in [(0,15,0),(-1,15,0),(-5,15,0),(5,15,0),(-5,5,0),(-5,15,1)]:
 m=Machine(Image(raw));m.allowed=[(0x477300,0x4773e6),(0x588ab0,0x588bf5),(0x584e30,0x584f60),(0x5abdaf,0x5abf00)]
 physics,body,owner=0x1100000,0x1101000,0x1102000
 m.put(physics+0x20,body);m.put(physics+0x58,owner);m.put(physics+0x5c,owner-0x40)
 m.put(0x6d4ac4,0x5abdb5);m.put(0x734540,0);m.put(0x735fac,0)
 m.put(body+0x144,1.22,'f');m.put(body+0xa8,speed,'f')
 for k in range(3):m.put(body+0x30+20*k,1.,'f')
 for k in range(4):
  strut=0x1103000+k*0x1000;setup=strut+0x200;wheel=strut+0x400
  m.put(physics+0x28+4*k,strut);m.put(strut+0x14,setup)
  m.put(strut+0x1c,.1 if mask&(1<<k) else 0,'f');m.put(setup+0x14,.1,'f')
  m.put(strut+0xa4,body);m.put(strut+0xa0,wheel);m.put(wheel+0x148,1.,'f')
 m.hook(0x47ac00,'supplied-rest-result',value=rest)
 m.hook(0x46abd0,'observed-completion-refresh')
 m.run(0x477300,(physics,))
 values=[m.get(owner+0x2f0+4*k,'f') for k in range(4)]
 rows.append(dict(speed=speed,mask=mask,rest=rest,values=values,hooks=m.events,momentum=[m.get(body+0x70+4*k,'f') for k in range(3)]))
 print(rows[-1])
assert rows[0]['values']==[0]*4 and rows[3]['values']==[0]*4 and rows[5]['values']==[0]*4
assert all(x>1 for x in rows[2]['values']) and rows[4]['values'][1]==rows[4]['values'][3]==0
(args.out/'bump-publish-native.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
#476240 accumulates the same scalar for the separate AI component system.
for speed,mask in ((0,15),(-1,15),(-5,15),(5,15),(-5,5)):
 m=Machine(Image(raw));m.allowed=[(0x476240,0x476460),(0x584e30,0x584f60),(0x5abdaf,0x5abf00)]
 physics,body,owner,setup=0x1100000,0x1101000,0x1102000,0x1103000
 m.put(physics+0x14,body);m.put(physics+0x18,setup);m.put(0x6d4ac4,0x5abdb5)
 m.put(body+0x144,1.22,'f');m.put(body+0xa8,speed,'f')
 for k in range(3):m.put(body+0x30+20*k,1.,'f')
 for k in range(4):
  m.put(physics+0xc8+4*k,.1 if mask&(1<<k) else 0,'f')
  m.put(physics+0x34+4*k,.1,'f');m.put(physics+0xe8+16*k,1.,'f')
 m.run(0x476240,(physics,),eax=owner)
 values=[m.get(owner+0x2f0+4*k,'f') for k in range(4)]
 expected=next(r['values'] for r in rows if r['speed']==speed and r['mask']==mask and r['rest']==0)
 assert values==expected,(speed,mask,values,expected)
 rows.append(dict(selector=3,speed=speed,mask=mask,values=values,hooks=m.events))
 print(rows[-1])
(args.out/'bump-publish-native.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
assert (retail/'speed.exe').read_bytes()==raw
print('PASS original player and AI per-wheel accumulation; explicit player rest/refresh hooks only')
