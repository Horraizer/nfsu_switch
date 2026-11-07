"""Original magazine tuning-task gates on supplied resolved installed parts.
The RideInfo resolver is a declared input boundary copying actual retail part
records; native task predicates and reputation math execute without replacement.
"""
from pathlib import Path
import argparse,hashlib,struct,sys,json,random
sys.dont_write_bytecode=True
from world_body_trace import Image,chunks,jdlz
from physics256_native import Machine
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();assert out.relative_to(Path.cwd()).as_posix().isascii() and a.root.resolve() not in out.parents;out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes();front=jdlz((a.root/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
im=Image(raw);m=Machine(im);m.allowed=[(0x5a0060,0x5a012c),(0x51b060,0x51b720),(0x51aed0,0x51b060),(0x5a05b0,0x5a0868),(0x417600,0x417720),(0x57d160,0x57d1f8)]
at,end=next((x,y) for t,x,y in chunks(front) if t==0x80034a00);m.uc.mem_write(0x1010000,front[at:end]);m.run(0x5a00a0,(0x1010000,))
# Actual CRT68ECA0 dictionary stores RACE1..11 then TASK1..16.
for index in range(27):
 key=0xffffffff
 for c in ('MAGAZINE_'+('RACE_'+str(index+1) if index<11 else 'TASK_'+str(index-10))).encode():key=(key*33+c)&0xffffffff
 m.put(0x744ab0+index*8,key);m.put(0x744ab4+index*8,index)
at,end=next((x,y) for t,x,y in chunks(bun) if t==0x34604);records=[bun[p:p+48] for p in range(at+8,end,48)]
slots=[1,2,6,7,8,10,11,13,14,15,17,30,31,58,115,116]
car_hash=0xffffffff
for c in b'CIVIC':car_hash=(car_hash*33+c)&0xffffffff
def hashed(name):
 value=0xffffffff
 for c in name.encode('ascii'):value=(value*33+c)&0xffffffff
 return value
shared={15:hashed('EXHAUST'),17:hashed('WHEELS'),115:hashed('WINDOW_TINTS'),116:hashed('NEON')}
def compatible(record,slot):
 car=struct.unpack_from('<I',record,4)[0]
 if slot==30:return struct.unpack_from('<I',record,12)[0] in (0x02daab07,0x03437a52,0x03797533)
 return slot>=30 or car==car_hash or car==shared.get(slot,-1)
choices={s:[i for i,r in enumerate(records) if r[16]==s and compatible(r,s)] for s in slots}
rng=random.Random(0x51b060);cases=[[]]
for level in (0,1,2,3,4):
 cases.append([next(i for i in choices[s] if records[i][17]==level) for s in slots if any(records[i][17]==level for i in choices[s])])
mixed=[]
for slot in slots:
 wanted=4 if slot in (6,8,11,17,31) else 3
 eligible=[i for i in choices[slot] if records[i][17]==wanted]
 if eligible:mixed.append(eligible[0])
cases.append(mixed)
for _ in range(96):cases.append([rng.choice(choices[s]) for s in slots if choices[s] and rng.randrange(6)])
supplied=bytearray(0x730)
def resolve(cpu):
 target=cpu.get(cpu.reg('esp')+12);cpu.uc.mem_write(target,bytes(supplied));cpu.ret(12)
m.hook(0x4ac5a0,'supplied_resolved_actual_installed_parts',callback=resolve)
rows=[]
for indices in cases:
 supplied[:]=bytes(0x730);m.uc.mem_write(0x7602a0,bytes(27*0x454+64));m.uc.mem_write(0x76777c,bytes(64))
 for index in indices:
  part=records[index];slot=59 if part[16]==58 else int(part[16]);address=0x1120000+slot*48;m.uc.mem_write(address,part);struct.pack_into('<I',supplied,0x44c+slot*4,address)
 m.run(0x51b060)
 mask=sum((1<<task) for task in range(16) if m.get(0x7602a1+(11+task)*0x454,'B'))
 rows.append([indices,mask])
text='// Original51B060 tuning-task predicates, actual parts, supplied RideInfo.\nstruct NativeMagazineTask {std::array<unsigned,16> parts;unsigned mask;};\nstatic constexpr NativeMagazineTask kNativeMagazineTasks[]={\n'
for indices,mask in rows:text+='    {{'+','.join(str(i)+'U' for i in indices+[0xffffffff]*(16-len(indices)))+'},'+str(mask)+'U},\n'
text+='};\n'
performance=[]
for level in range(4):
 for drop in range(-1,9):
  values=[level]*9
  if drop>=0:values[drop]=max(0,level-1)
  m.uc.mem_write(0x7602a0,bytes(27*0x454+64));m.uc.mem_write(0x76777c,bytes(64))
  for index,value in enumerate(values):m.put(0x75f1d8+4*index,value)
  m.run(0x51aed0)
  mask=sum(1<<task for task in range(3) if m.get(0x7602a1+(11+task)*0x454,'B'))
  performance.append(values+[mask])
text+='static constexpr unsigned kNativeMagazinePerformance[][10]={\n'+''.join('    {'+','.join(str(v)+'U' for v in row)+'},\n' for row in performance)+'};\n'
target=Path('tests/career_magazine_tasks_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'magazine task fixture stale'
(out/'oracle.json').write_text(json.dumps(dict(rows=rows,performance=performance,hooks=sorted(set(e['hook'] for e in m.events)),boundary=__doc__),indent=2)+'\n',encoding='ascii')
print('PASS native magazine tuning tasks:',len(rows),'actual installed configurations')
