"""Bounded original installed-car visual reputation oracle. Retail is read-only."""
from pathlib import Path
import sys,struct,math,json,hashlib,argparse,random
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,jdlz,chunks
from physics256_native import Machine
from unicorn import x86_const as x
parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--write-fixture',action='store_true');a=parser.parse_args();retail=a.root;relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');assert retail.resolve() not in a.out.resolve().parents;a.out.mkdir(parents=True,exist_ok=False);raw=(retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';im=Image(raw);m=Machine(im)
m.allowed=[(0x5a0060,0x5a012c),(0x5a05b0,0x5a0868)]
f=jdlz((retail/'FrontEnd/FrontB.lzc').read_bytes());at,end=next((a,b) for t,a,b in chunks(f) if t==0x80034a00);m.uc.mem_write(0x1010000,f[at:end]);m.run(0x5a00a0,(0x1010000,))

ride=0x1100000;part=0x1110000;rows=[]
def call(levels):
 m.uc.mem_write(ride,bytes(0x1000));m.uc.mem_write(part,bytes(120*48));m.uc.reg_write(x.UC_X86_REG_FPTAG,0xffff);m.uc.reg_write(x.UC_X86_REG_FPSW,0)
 for slot,level in levels:
  m.put(part+slot*48+17,level,'B');m.put(ride+0x44c+slot*4,part+slot*48)
 m.run(0x5a05b0,edi=ride)
 top=(m.uc.reg_read(x.UC_X86_REG_FPSW)>>11)&7;mantissa,exp=m.uc.reg_read(x.UC_X86_REG_FP0+top)
 return (-1 if exp&0x8000 else 1)*math.ldexp(float(mantissa),(exp&0x7fff)-16383-63) if mantissa else 0.0

bun=(retail/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
assert hashlib.sha256(f).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
at,end=next((a,b) for t,a,b in chunks(bun) if t==0x34604);records=[bun[p:p+48] for p in range(at+8,end,48)]
slots=(15,17,115,10,13,11,1,2,8,7,116,14,6,30,58,31);choices={s:[i for i,r in enumerate(records) if r[16]==s] for s in slots};cases=[[]]
for slot in slots:
 for level in range(5):
  found=next((i for i in choices[slot] if records[i][17]==level),None)
  if found is not None:cases.append([found])
rng=random.Random(0x5a05b0)
for _ in range(128):cases.append([rng.choice(choices[s]) for s in slots if choices[s] and rng.randrange(3)!=0])
rows=[];lines=['// Original PC1.4 USA5A05B0, actual part levels and supplied installed slots.', 'struct NativeTuningReputation {std::array<unsigned,16> indices;float value;};','static constexpr NativeTuningReputation kNativeTuningReputation[]={']
for indices in cases:
 levels=[(records[i][16] if records[i][16]!=58 else 59,records[i][17]) for i in indices];value=call(levels);assert math.isfinite(value)
 row=indices+[0xffffffff]*(16-len(indices));rows.append(dict(indices=indices,value=value))
 lines.append('    {{'+','.join(str(i)+'U' for i in row)+'},'+format(value,'.9g')+('F' if value%1 else '.0F')+'},')
lines+=['};'];text='\n'.join(lines)+'\n';fixture=root/'tests/career_tuning_reputation_native.inc'
if a.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'reputation fixture stale'
assert not m.events
(a.out/'reputation.json').write_text(json.dumps(dict(cases=rows,hooks=m.events,boundary='Original5A05B0, native price loader, supplied RideInfo pointers using actual record levels. No live garage or hidden retail calls.'),indent=2)+'\n',encoding='ascii')
print('PASS original visual reputation:',len(cases),'actual-record configurations; no hooks')
