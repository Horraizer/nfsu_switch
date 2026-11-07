"""Original magazine recipe selector with actual retail pool loader.
Only malloc/free are substituted by a bounded private arena. No selection,
RNG, linked-list traversal or caption/camera choice is replaced. Supplied
seed/award hash are explicit; no live career/renderer equivalence is claimed.
"""
from pathlib import Path
import argparse,hashlib,struct,sys,json
sys.dont_write_bytecode=True
from world_body_trace import Image,chunks
from physics256_native import Machine
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();assert out.relative_to(Path.cwd()).as_posix().isascii()
assert a.root.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
first,last=next((x,y) for tag,x,y in chunks(bun) if tag==0x30230)
image=Image(raw);m=Machine(image)
m.allowed=[(0x51bfd0,0x51c020),(0x51c180,0x51c33f),(0x51a8d0,0x51acb0),(0x51c480,0x51c5d0),(0x40a880,0x40a890),(0x5a0bd0,0x5a0ce0),(0x4c94b0,0x4c9590)]
m.run(0x51bfd0,eax=0x744238)
m.uc.mem_write(0x1010000,bun[first:last]);m.run(0x51c180,(0x1010000,));assert m.reg('eax')==1
arena=[0x1150000]
def allocate(cpu):
 size=cpu.get(cpu.reg('esp')+4);at=arena[0];arena[0]+=(size+15)&~15
 assert arena[0]<0x1180000
 cpu.uc.mem_write(at,bytes(size));cpu.ret(value=at)
m.hook(0x67121a,'private_malloc',callback=allocate);m.hook(0x671102,'private_free')
rows=[]
for family,count in [('RACE',11),('TASK',16)]:
 for number in range(1,count+1):
  name='MAGAZINE_'+family+'_'+str(number);key=0xffffffff
  for c in name.encode('ascii'):key=(key*33+c)&0xffffffff
  for seed in (0,1,2,0x60000000,0xffffffff):
   arena[0]=0x1150000;m.put(0x6f227c,seed);m.run(0x51a8d0,(name_key:=key,),eax=0)
   result=m.reg('eax');assert result
   chosen=[m.get(result+offset) for offset in (0x10,0x14,0x1c,0x18,0x20,0x24)]
   # Index each actual selected native linked record in the loaded input pool.
   counts=struct.unpack_from('<8I',bun,first+8);sizes=[12,16,168,16,20,56,360,80]
   starts=[];at=0x1010000+40
   for n,size in zip(counts,sizes):starts.append(at);at+=n*size
   indices=[(pointer-starts[pool])//sizes[pool] if pointer else -1 for pointer,pool in zip(chosen,(1,2,3,4,5,6))]
   assert all(i>=-1 for i in indices)
   random_after=m.get(0x6f227c)
   photo=0x1140000;m.uc.mem_write(photo,bytes(0x454));m.run(0x5a0bd0,(result,),esi=photo)
   #51BC80 receives348 in EAX,35C/36C on stack.513C90 binds35C
   #to MagazineFront; the separate background scene consumes348.
   textures=[m.get(photo+offset) for offset in (0x348,0x35c,0x36c)]
   rows.append([key,seed,random_after]+indices+textures+[m.get(photo+0x34c)])
report=dict(rows=rows,hooks=sorted(set(e['hook'] for e in m.events)),boundary=__doc__)
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
text='// Original51C180 and51A8D0/51C480; bounded allocation only.\nstatic constexpr long long kNativeMagazineChoices[][13]={\n'+''.join('    {'+','.join(str(v)+'LL' for v in row)+'},\n' for row in rows)+'};\n'
target=Path('tests/career_magazine_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'magazine fixture stale'
print('PASS native magazine recipe selection:',len(rows),'cases; only bounded allocation hooks')
