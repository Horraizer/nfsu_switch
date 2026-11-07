"""Run original x86 engine/HUD slices on selected portable handling and captured inputs.
This checks arithmetic, not original tuning selection or complete trajectories.
"""
from pathlib import Path
import sys,hashlib,struct,csv,json,argparse
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine,STACK,bits
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--captures',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();captures=a.captures.resolve();output_path=a.out.resolve();retail=a.root.resolve()/'speed.exe'
assert not output_path.exists() and a.root.resolve() not in output_path.parents
b=retail.read_bytes();digest=hashlib.sha256(b).hexdigest();assert digest=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(b))
m.allowed=[(0x46c7c0,0x46c912),(0x46c6d0,0x46c764),(0x478365,0x478395),(0x46cf80,0x46cfb8),(0x46cff0,0x46d1b1),(0x583ea0,0x583f18),(0x674898,0x67490d),(0x4387e0,0x4387f6),(0x46aa40,0x46ac08),(0x46c770,0x46c7c0),(0x4551b0,0x4551b6),(0x40a880,0x40a885),(0x575940,0x575945),(0x46ca90,0x46cf71),(0x478fbb,0x479051)]
engine,setup,physics,actor,gearbox,system,vtable,output=[0x1100000+k*0x1000 for k in range(8)];state=actor+0x40
record=(captures/'handling.bin').read_bytes();m.uc.mem_write(setup,record[0x110:])
m.put(physics+0x58,state);m.put(physics+0x48,engine);m.put(physics+0x54,setup)
m.put(0x78a344,0,'B');m.put(0x7361a8,0);m.put(0x73ad34,0);m.put(0x7000e4,100.,'f')
m.run(0x46c7c0,(state,),esi=engine,ecx=setup+0x1f0);m.put(engine+0x20,gearbox);m.put(gearbox+0x44,2)
m.put(system,vtable);m.put(vtable+4,0x46abd0);m.put(system+0xc,engine+4);m.put(engine+4,system+0xc)
rows=list(csv.DictReader((captures/'engine.csv').open()));diff=[]
for j,row in enumerate(rows):
 v={k:float(x) for k,x in row.items()};m.put(state+0x3a0,v['rpm'],'f')
 m.run(0x478365,end=0x478395,esi=physics)
 for offset,key in [(0x78,'throttle'),(0x80,'multiplier'),(0x7c,'feedback'),(0x9c,'cutoff')]:m.put(engine+offset,v[key],'f')
 m.put(engine+0xbc,0.,'f');m.put(gearbox+0x3c,int(v['gear']))
 m.run(0x46cf80,(output,),ecx=engine);derivative=m.get(output,'f')
 m.run(0x46aa40,(system,bits(1/240)));speed=m.get(engine+0x14,'f')
 m.put(STACK+4,1/60,'f');m.run(0x478fbb,end=0x479051,eax=engine,edi=physics,ebp=STACK-4);published=m.get(state+0x3a0,'f')
 for key,x in [('derivative',derivative),('next_rad_s',speed),('published_rpm',published)]:
  if x!=v[key]:diff.append({'case':j,'field':key,'original':x,'port':v[key],'input':v})
assert not m.events
folder=captures
cutoff_diff=[];cutoff_rows=list(csv.DictReader((folder/'cutoff.csv').open()))
for j,row in enumerate(cutoff_rows):
 v={k:float(x) for k,x in row.items()}
 m.put(engine+0x14,v['rad_s'],'f');m.put(gearbox+0x3c,int(v['gear']));m.put(engine+0x9c,v['before'],'f');m.put(0x73ad34,int(v['ticks']))
 m.run(0x46ca90,ecx=engine)
 value=m.get(engine+0x9c,'f')
 if value!=v['after']:cutoff_diff.append({'case':j,'inputs':v,'original':value})
result={'engine_cases':len(rows),'engine_differences':diff,'cutoff_cases':len(cutoff_rows),'cutoff_differences':cutoff_diff}
m.allowed.extend([(0x4a0fb9,0x4a0ffe),(0x4a101a,0x4a1026)])
hud=0x1110000;m.put(hud+0x10,1);m.put(hud+0x18,1);m.put(0x78a3f5,0,'B')
speed_diff=[];speed_rows=list(csv.DictReader((folder/'speed.csv').open()))
for j,row in enumerate(speed_rows):
 m.put(actor+0x3d4,float(row['published']),'f');m.run(0x4a0fb9,end=0x4a0ffe,ebp=actor,ebx=hud)
 scaled=m.get(STACK+0x14,'f');m.put(STACK+0x18,scaled,'f');m.run(0x4a101a,end=0x4a1026)
 value=m.reg('eax')
 if value!=int(row['mph']):speed_diff.append({'case':j,'port':row,'original':value})
result.update(speed_cases=len(speed_rows),speed_differences=speed_diff,executable_sha256=digest,hooks=m.events)

result['scope']='Original arithmetic with portable selected handling and supplied inputs; no full live trajectory, input timing or tuning selection parity.'
result['inputs']={name:hashlib.sha256((captures/name).read_bytes()).hexdigest() for name in ['handling.bin','engine.csv','cutoff.csv','speed.csv']}
output_path.write_text(json.dumps(result,indent=2)+'\n',encoding='ascii')
assert hashlib.sha256(retail.read_bytes()).hexdigest()==digest
assert len(rows)==108 and len(cutoff_rows)==36 and len(speed_rows)==10
assert not m.events and not diff and not cutoff_diff and not speed_diff, 'original/port mismatch; see report'
print('PASS: engine108, cutoff36, HUDspeed10 original comparisons; no external hooks')
