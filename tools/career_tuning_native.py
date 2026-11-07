"""Bounded original PC1.4 USA tuning catalog oracle.
Read-only inputs; original loader/price/category/availability functions with
supplied career contexts and no external hooks. Not live garage acceptance.
"""
from pathlib import Path
import sys,struct,json,hashlib,argparse
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,jdlz,chunks
from physics256_native import Machine
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
a=parser.parse_args();retail=a.root;out=a.out
relative=out.resolve().relative_to(root)
assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
assert retail.resolve() not in out.resolve().parents
out.mkdir(parents=True,exist_ok=False)
raw=(retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);m=Machine(im);m.allowed=[(0x5a0060,0x5a012c),(0x5a0350,0x5a0445),(0x5a08b0,0x5a092c),(0x5a1550,0x5a162a),(0x4aa970,0x4aa9c7),(0x4aa9d0,0x4aaab1)]
stored=(retail/'FrontEnd/FrontB.lzc').read_bytes();global_bytes=(retail/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(jdlz(stored)).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
assert hashlib.sha256(global_bytes).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
data=jdlz(stored)
container_rows=[(at,end) for tag,at,end in chunks(data) if tag==0x80034a00];assert len(container_rows)==1
container,container_end=container_rows[0];size=container_end-container-8;front=0x1010000
event_rows=[(at,end) for tag,at,end in chunks(data) if tag==0x34a02];assert len(event_rows)==1
event_at,event_end=event_rows[0];events=[list(struct.unpack_from('<83I',data,p)) for p in range(event_at+8,event_end,332)]
m.uc.mem_write(front,data[container:container+size+8]);m.run(0x5a00a0,(front,));assert m.reg('eax')==1
p=0x1100000;part=0x1110000;costs=[];slots=[];performance=[]
for slot in range(120):
 m.run(0x5a03d0,eax=slot);cat=m.reg('eax');slots.append(cat if cat<32 else -1)
 if cat>=32:continue
 for level in range(5):
  m.uc.mem_write(part,bytes(48));m.put(part+16,slot,'B');m.put(part+17,level,'B');m.run(0x5a08f0,ecx=part);costs.append((slot,level,m.reg('eax')))
for field in range(11):m.run(0x5a0350,eax=field);performance.append(m.reg('eax'))
unlocks=[]
for cat in range(25):
 for level in range(5):
  grants=[]
  for row in events:
   for i in range(row[29]):
    if row[14+3*i:17+3*i]==[0,cat,level]:grants.append(row[0])
  for case in (0,1,2):
   m.uc.mem_write(p,bytes(0x9000));m.put(p+0x34c,2,'B')
   if case==1:
    for event in range(1,6):m.put(p+0x34c+event,2,'B')
   if case==2 and grants:m.put(p+0x34c+min(grants),2,'B')
   m.run(0x5a1550,(p,cat),edi=level);unlocks.append((cat,level,case,int(bool(m.reg('eax')&255))))
style_rows=[(at,end) for tag,at,end in chunks(global_bytes) if tag==0x34a08];assert len(style_rows)==1
style_at,style_end=style_rows[0];first=(style_at+23)&~15
style=[list(struct.unpack_from('<8I',global_bytes,p)) for p in range(first,style_end-31,32)];source=0x10a0000;m.uc.mem_write(source,b''.join(struct.pack('<8I',*r) for r in style));m.put(0x7343f8,source);m.put(0x7343fc,len(style));styles=[]
for i,row in enumerate(style):
 threshold=row[0];kind=row[1]&65535;key=row[2]
 for points in (threshold-1,threshold,threshold+1):
  m.uc.mem_write(p,bytes(0x9000));m.run(0x4aa970,(0,points),ebx=p+0x214)
  m.run(0x4aa9d0,(p,kind,key));styles.append((kind,key,points,int(bool(m.reg('eax')&255))))
# Earned flags stay unlocked even after the point bank is lower.
m.uc.mem_write(p,bytes(0x9000));m.run(0x4aa970,(0,style[0][0]),ebx=p+0x214);m.run(0x4aa970,(0,0),ebx=p+0x214);m.run(0x4aa9d0,(p,0,style[0][2]));sticky=bool(m.reg('eax')&255);assert sticky
assert not m.events
report=dict(slot_categories=slots,performance_categories=performance,prices=costs,unlocks=unlocks,style=styles,sticky=sticky,hooks=m.events,boundary='Original initialized tables and supplied career/point contexts; no live garage claim')
(out/'native-catalog-root-r2.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Original PC1.4 USA native catalogue witnesses; see CAREER_TUNING.md.','static constexpr int kNativeTuningSlotCategory[]={'+','.join(map(str,slots))+'};','static constexpr unsigned kNativeTuningPerformanceCategory[]={'+','.join(map(str,performance))+'};']
for name,rows in [('Price',costs),('Unlock',unlocks),('Style',styles)]:
 lines+=['static constexpr unsigned kNativeTuning'+name+'[][ '+str(len(rows[0]))+' ]={']+['    {'+','.join(str(v)+'U' for v in r)+'},' for r in rows]+['};']
fixture=root/'tests/career_tuning_native.inc';text='\n'.join(lines)+'\n'
if a.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'native fixture is stale'
print('PASS native catalog:',len(costs),'prices,',len(unlocks),'event gates,',len(styles),'style gates; sticky flag retained; no hooks')
