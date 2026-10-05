"""Authenticated FE moving-road light pool witness; explicit startup inputs.
No full retail startup/default claim. Supplies original FrontB marker nodes,
then executes genuine4AE1F0 including4AE530 and native32-slot population.
"""
from pathlib import Path
import sys,struct,hashlib,argparse
sys.dont_write_bytecode=True
from event0_trace import Image,jdlz,chunks,name_hash
from hud254_native import Machine
from unicorn.x86_const import *
p=argparse.ArgumentParser();p.add_argument('--retail-root',type=Path,required=True)
p.add_argument('--road-offset',type=float,default=None)
args=p.parse_args();root=args.retail_root
assert args.road_offset is None or 0<=args.road_offset<200
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
front=jdlz((root/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
markers=[]
for tag,a,end in chunks(front,0,381824):
 if tag!=0x80134010:continue
 name='';records=[]
 for t,p,q in chunks(front,a+8,end):
  if t==0x134011:
   aligned=(p+8+15)&~15;name=front[aligned+0x94:aligned+0xb0].split(b'\0')[0].decode('ascii')
  if t==0x13401a:
   aligned=(p+8+15)&~15
   assert aligned<=q and (q-aligned)%80==0 and (q-aligned)//80<=127
   assert all(v==0x11 for v in front[p+8:aligned])
   for at in range(aligned,q,80):records.append((struct.unpack_from('<I',front,at)[0],struct.unpack_from('<16f',front,at+16)))
 if name=='TRFE_ROAD02_A':markers=[matrix for h,matrix in records if h==name_hash('TRFE_ROAD02_FLARE')]
assert len(markers)==4
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
for offset in ((0.,99.,101.,199.) if args.road_offset is None else (args.road_offset,)):
 m=Machine(im);env=0x1001000
 m.uc.mem_write(env,struct.pack('<7f',200.,offset,0.,50.,50.,0.,1.))
 m.write(0x7364dc,1);m.write(0x7404c4,1)
 m.uc.mem_write(0x73ad38,struct.pack('<f',0));m.write(0x73ad3c,0)
 m.uc.mem_write(0x735e90,struct.pack('<f',.125))
 for at,value in ((0x735ea0,17),(0x735ea4,39),(0x735ea8,71)):m.write(at,value)
 for at,value in ((0x735e94,.4),(0x735e98,.03),(0x735e9c,.001)):m.uc.mem_write(at,struct.pack('<f',value))
 for group in range(2):
  sentinel=env+0xd50+group*0x2e0+0x2d8
  nodes=[0x1004000+group*0x1000+i*0x80 for i in range(4)]
  m.write(sentinel,nodes[0]);m.write(sentinel+4,nodes[-1])
  for i,(node,matrix) in enumerate(zip(nodes,markers)):
   m.write(node,nodes[i+1] if i<3 else sentinel)
   m.write(node+8,0x12340000+i)
   m.uc.mem_write(node+0x18,struct.pack('<3f',*matrix[12:15]))
   m.uc.mem_write(node+0x28,struct.pack('<3f',*matrix[8:11]))
 m.run(0x4ae1f0,(),{UC_X86_REG_EAX:env})
 active=[]
 for i in range(32):
  at=env+0x58+i*0x68;state=bytes(m.uc.mem_read(at+4,4))
  if not state[3]:continue
  assert state==bytes((2,2,0,1))
  color=struct.unpack('<I',m.uc.mem_read(at+12,4))[0]
  assert color==0xff472711
  position=struct.unpack('<3f',m.uc.mem_read(at+16,12))
  rest=struct.unpack('<8f',m.uc.mem_read(at+28,32))
  active.append((i,position,rest))
 assert len(active)==20
 print('NATIVE_ROAD_LIGHT_POOL',offset,active)
 # Original4ADA60 node wiring: road+1C in743C10, active flag+44,
 # intrusive32-record sentinel+48; each record payload begins node+8.
 m.write(0x740a68,0x740a68);m.write(0x743c10,env+0x1c);m.write(0x743c14,env+0x1c)
 m.write(env+0x1c,0x743c10);m.write(env+0x20,0x743c10);m.write(env+0x44,1)
 sentinel=env+0x48;nodes=[env+0x50+i*0x68 for i in range(32)]
 m.write(sentinel,nodes[0]);m.write(sentinel+4,nodes[-1])
 for i,node in enumerate(nodes):
  m.write(node,nodes[i+1] if i<31 else sentinel)
  m.write(node+4,nodes[i-1] if i else sentinel)
 eye=0x1008000;out=0x1008100;m.uc.mem_write(eye,struct.pack('<3f',0,0,.20027998))
 m.run(0x5413e0,(out,),{UC_X86_REG_EAX:eye})
 count=m.uc.reg_read(UC_X86_REG_EAX)
 print('NATIVE_NEARBY_ROAD_LIGHT_COUNT',offset,count)
 assert count<=10

print('PASS: native moving pool; marker inputs original, RGB/height/attenuation explicit test boundaries')
