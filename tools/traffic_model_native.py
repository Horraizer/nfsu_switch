from pathlib import Path
import sys,hashlib,json,struct
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine,STACK
import argparse
ap=argparse.ArgumentParser(description='Actual traffic model, duplicate and skin/RNG boundaries; supplied config list')
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
root=args.retail_root
raw=(root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bundle=(root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(bundle).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9';records=[]
for tag,first,last in chunks(bundle):
 if tag==0x34600:
  for at in range((first+8+15)&~15,last,0xc90):
   if at+0xc90<=last:records.append(bundle[at:at+0xc90])
assert len(records)==35
im=Image(raw);table,pool=0x1100000,0x1140000
metadata=[]
for i,r in enumerate(records):
 metadata.append(dict(index=i,id=struct.unpack_from('<I',r,0xc50)[0],name=r[:16].split(b'\0')[0].decode('ascii'),category=struct.unpack_from('<I',r,0xc54)[0],enabled=r[0x40]!=0,variants=struct.unpack_from('<b',r,0xd9)[0],weights=list(struct.unpack_from('<5f',r,0xc60)),skins=list(struct.unpack_from('<16b',r,0xc80))))
rows=[]
for track in [1003,1010,10,20,30,40,60]:
 for seed in [0,1,0x12345678,0x60000000,0x7fffffff,0xffffffff]:
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.uc.mem_write(table,b''.join(records));m.put(0x734588,table);m.put(0x78a2f0,track);m.put(0x6f227c,seed);m.put(0x78a418,pool)
  used=[]
  for actor in range(10):
   m.put(0x78a41c,len(used))
   for j,index in enumerate(used):m.put(pool+j*0x750,0,'B');m.put(pool+j*0x750+0x10,index)
   before=m.get(0x6f227c)
   m.run(0x4b3b20,end=0x4b3b74)
   selected=m.reg('edi');attempts=m.get(STACK+0x1c)
   assert selected<len(records)
   model_seed=m.get(0x6f227c)
   m.run(0x4b3b7d,end=0x4b3bee,edi=selected)
   rows.append([track,seed,actor,before,selected,attempts,model_seed,m.reg('eax'),m.get(0x6f227c)])
   used.append(selected)
  assert not m.events
fixture='// Actual57D000/57CFA0/564A80, duplicate4B3B20 and skin4B3B7D; no hooks.\nstatic constexpr unsigned kNativeTrafficModels[][9]={\n'
fixture+=''.join('    {'+','.join('0x%08XU'%(n&0xffffffff) for n in row)+'},\n' for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/traffic_model_native.inc'
if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
else:assert path.read_text(encoding='ascii')==fixture,'native traffic model fixture differs'
print('PASS actual traffic model/duplicate/skin boundaries:',len(rows),'rows; supplied config/seed, no actor constructors')
