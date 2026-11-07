"""Read-only PC1.4 USA unique reward producer and transaction witness."""
from pathlib import Path
import sys,json,struct,hashlib,argparse
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,jdlz,chunks
from physics256_native import Machine,ACTOR,DRIVER,CONFIG
ap=argparse.ArgumentParser(description='Original unique availability/grant/voice on supplied saved contexts')
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
retail=a.root;out=a.out.resolve();relative=out.relative_to(Path.cwd())
assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
assert retail.resolve()!=out and retail.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);front=jdlz((retail/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
regions=[(at,end) for tag,at,end in chunks(front) if tag==0x80034a00];assert len(regions)==1
at,end=regions[0];m=Machine(im)
m.allowed=[(0x5a0060,0x5a012c),(0x5a1550,0x5a162a),(0x4c7360,0x4c73e0),(0x4c6c00,0x4c6c38),(0x4c76d0,0x4c76fc)]
m.uc.mem_write(0x1010000,front[at:end]);m.run(0x5a00a0,(0x1010000,));assert m.reg('eax')==1
mapping=[struct.unpack('<4I',im.read(at,16)) for at in range(0x6fad60,0x6fadf0,16)]
eligibility=[];grants=[];speech=[]
for completed in (0,5,7,15,25,26,30):
 m.uc.mem_write(ACTOR,bytes(0x9000))
 for event in range(completed+1):m.put(ACTOR+0x34c+event,2,'B')
 for cat,field,icon,language in mapping:
  m.run(0x5a1550,(ACTOR,cat),edi=4)
  eligibility.append([completed,cat,field,int(bool(m.reg('eax')&255))])
for cat,field,icon,language in mapping:
 m.uc.mem_write(0x75f204,bytes(36));m.put(DRIVER+8,cat);m.put(CONFIG+0x40,DRIVER)
 m.run(0x4c7360,end=0x4c7396,edx=CONFIG)
 values=[m.get(0x75f204+4*k) for k in range(9)]
 assert values==[int(k==field) for k in range(9)]
 grants.append([cat,field,icon,language]);assert m.get(CONFIG+0x88,'B')==1
for event in range(112):
 values=[]
 for entry in (0x4c6c00,0x4c76d0):
  m.run(entry,eax=event);value=m.reg('eax');values.append(value if value<0x80000000 else value-0x100000000)
 speech.append([event]+values)
bun=(retail/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
part_regions=[(at,end) for tag,at,end in chunks(bun) if tag==0x34604];assert len(part_regions)==1
first,last=part_regions[0];h=0xffffffff
for c in b'CIVIC':h=(h*33+c)&0xffffffff
visual=[];part=0x1110000
m.allowed += [(0x4c8720,0x4c87bf),(0x5a2620,0x5a2690),(0x5a03d0,0x5a0445),(0x5a34d0,0x5a354c),(0x4bbd70,0x4bbd9c),(0x5a3630,0x5a366d)]
for at in range(first+8,last,48):
 record=bun[at:at+48]
 if record[16]!=8 or record[17]!=4 or struct.unpack_from('<I',record,4)[0]!=h:continue
 m.uc.mem_write(0x75eef8,bytes(0x9000));m.put(0x75eef8+0x1374,5000)
 m.uc.mem_write(part,record);m.put(CONFIG+0x7c,DRIVER);m.put(DRIVER+8,part)
 m.run(0x4c8720,end=0x4c87bf,ebx=CONFIG)
 cost=m.get(0x75eef8+0x1374);spent=m.get(0x75eef8+0x137c)
 owned=m.get(0x75eef8+0x44c+4+12*144)
 assert cost==5000 and spent==0 and owned==(12|(record[18]<<8)|(4<<16))
 visual.append([struct.unpack_from('<I',record,8)[0],owned,cost,spent])
assert visual,'no actual Civic unique hood records exercised'
assert not m.events
menus=[]
for kind,entry,flag in ((0,0x4c74d0,0x8b),(1,0x4c8900,0x7b0)):
 ui=Machine(im);ui.allowed=[(0x4c7200,0x4c72d0),(0x4c73e0,0x4c7670),
  (0x4c85b0,0x4c86e0),(0x4c8810,0x4c8ab0),(0x4f6910,0x4f6952)]
 ui.put(0x73578c,0);ui.put(CONFIG+flag,1,'B')
 ui.put(CONFIG+0x40,DRIVER);ui.put(CONFIG+0x7c,DRIVER)
 ui.run(entry,(0x9120409e,),ecx=CONFIG)
 assert ui.get(CONFIG+0x40)==DRIVER and ui.get(CONFIG+0x7c)==DRIVER and ui.get(CONFIG+flag,'B')==1
 ui.run(entry,(0xc519bfc8,),ecx=CONFIG)
 assert ui.get(CONFIG+flag,'B')==0 and ui.get(CONFIG+0x88,'B')==0
 ui.run(entry,(0xc519bfc8,),end=0x4dc6e0,ecx=CONFIG)
 descriptor=ui.get(ui.reg('esp')+4)
 params=[ui.get(descriptor+4*k) for k in range(9)]
 assert params[0]==0x9da49222 and params[1]==0x639 and params[2]==0xf0aadebb and params[3]==0x541389e5 and not ui.events
 menus.append([kind]+params)
report=dict(menus=menus,visual=visual,mapping=grants,eligibility=eligibility,speech=speech,hooks=[],boundary='Original initialized event tables,5A1550 unique availability and4C7360 flag publication before frontend callbacks;4C6C00/4C76D0 reward voice. Native accept/scroll events with absent FEng context; stops before dialog creation. No UI rendering equivalence claim.')
p=out/'oracle.json';p.write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Original4C7360/5A1550,4C8720/5A2620 and4C6C00/4C76D0. No hooks.',
 '// Supplied selection/career state. The frontend callbacks are outside the grant slice.']
for name,rows in [('Mapping',grants),('Eligibility',eligibility),('Speech',speech[:31]),('Hood',visual),('Menu',menus)]:
 lines.append('static constexpr long long kNativeUnique'+name+'[][ '+str(len(rows[0]))+' ]={')
 for row in rows:lines.append('    {'+','.join(str(v)+'LL' for v in row)+'},')
 lines.append('};')
text='\n'.join(lines)+'\n';target=Path('tests/career_unique_native.inc');assert text.isascii()
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'unique award fixture stale' 
print('PASS native unique availability, flag grant, reward speech and actual Civic level4 hood ownership/cost:',len(visual))
