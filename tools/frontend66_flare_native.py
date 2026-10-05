"""Bounded original FE road sprite consumer, original marker dual records."""
from pathlib import Path
import sys,runpy,struct,argparse,math
sys.dont_write_bytecode=True
# Reuse the authenticated entry/cube basis witness and its native machine.
base=runpy.run_path(str(Path(__file__).with_name('frontend66_cube_native.py')))
m=base['m'];root=argparse.ArgumentParser();root.add_argument('--retail-root',type=Path,required=True)
retail=root.parse_args().retail_root
from event0_trace import jdlz,chunks,name_hash
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
front=jdlz((retail/'FrontEnd/FrontB.lzc').read_bytes());markers=[]
for tag,a,end in chunks(front,0,381824):
 if tag!=0x80134010:continue
 name='';records=[]
 for t,p,q in chunks(front,a+8,end):
  if t==0x134011:
   at=(p+8+15)&~15;name=front[at+0x94:at+0xb0].split(b'\0')[0].decode('ascii')
  if t==0x13401a:
   at=(p+8+15)&~15;assert at<=q and (q-at)%80==0 and (q-at)//80<=127
   assert all(v==0x11 for v in front[p+8:at])
   for pos in range(at,q,80):records.append((struct.unpack_from('<I',front,pos)[0],struct.unpack_from('<16f',front,pos+16)))
 if name=='TRFE_ROAD02_A':markers=[matrix for h,matrix in records if h==name_hash('TRFE_ROAD02_FLARE')]
assert len(markers)==4
env=0x1020000;model=0x1040000;texture=0x1080000
m.uc.mem_write(env,struct.pack('<7f',200,0,0,50,50,0,1))
m.write(0x7355b4,model);m.write(0x7355b8,model+0x1000)
m.write(0x72fa1c,texture)
descriptor=m.integer(0x6f4150+9*8)
assert descriptor==0x6f40d8
print('NATIVE_TYPE9_DESCRIPTOR',struct.unpack('<14fI',m.uc.mem_read(descriptor,60)))
# Genuine unit-model initializer; no matrix arithmetic is stubbed.
m.run(0x40b5e0)
assert bytes(m.uc.mem_read(0x78bcc0,64))==base['identity']
# Genuine4ADA60 sprite subtype9 list+1020; separate subtype2 light list+1028
# is not passed to4AE7D0. Ground cache is original negative markerZ.
for group in range(2):
 sentinel=env+0x1020+group*0x2e0;nodes=[0x1030000+group*0x1000+i*0x80 for i in range(4)]
 m.write(sentinel,nodes[0]);m.write(sentinel+4,nodes[-1])
 for i,(node,matrix) in enumerate(zip(nodes,markers)):
  m.write(node,nodes[i+1] if i<3 else sentinel);m.write(node+4,nodes[i-1] if i else sentinel)
  m.write(node+8,name_hash('TRFE_ROAD02_FLARE'));m.uc.mem_write(node+0xc,bytes((1,9,0,0)))
  m.uc.mem_write(node+0x10,struct.pack('<3f',*matrix[12:15]));m.uc.mem_write(node+0x1c,struct.pack('<f',-matrix[14]))
  m.uc.mem_write(node+0x20,struct.pack('<3f',*matrix[8:11]))

# Original cube projection publishes the six actual clipping planes.
for face in range(6):
 sc=0x1011000+face*0x1000;projection=0x1060000+face*0x1000
 m.run(0x40dd00,(sc,0,0,0),{UC_X86_REG_ESI:projection})
 m.write(sc,projection)
 print('CUBE_FRUSTUM',face,struct.unpack('<24f',m.uc.mem_read(projection+0xc0,96)))


# Actual mode0 CView scalar publication; near/far are existing witness boundary.
m.write(0x1006000+0x1c,0x100b000)
for reg,value in ((UC_X86_REG_EDI,0x7788e0),(UC_X86_REG_ESI,0x1006000),(UC_X86_REG_ESP,m.stack)):m.uc.reg_write(reg,value)
m.uc.emu_start(0x48753d,0x487576,count=100)
m.uc.mem_write(0x100b000+0xbc,struct.pack('<ff',.05,300.))
for view,width,height in ((1,1280,720),(4,320,240)):
 sc=0x1050000+(view-1)*0x1000;vp=0x1058000+(view-1)*0x100;projection=0x1068000+(view-1)*0x1000
 m.write(sc+4,view);m.write(sc+0x40,0x100b000);m.write(sc+0x58,vp)
 m.write(vp+0x14,width);m.write(vp+0x18,height)
 m.run(0x40dd00,(sc,0,0,0),{UC_X86_REG_ESI:projection});m.write(sc,projection)
draws=[];calls=[]
def capture(uc,at,size,_):
 if at==0x5447c0:
  sp=uc.reg_read(UC_X86_REG_ESP);calls.append(tuple(m.integer(sp+4+i*4) for i in range(6)))
 if at==0x40c300:
  sp=uc.reg_read(UC_X86_REG_ESP);args=[m.integer(sp+4+i*4) for i in range(3)]
  matrix=uc.reg_read(UC_X86_REG_EAX)
  try:
   quad=bytes(uc.mem_read(matrix,0x94))
   payload={'corners':[struct.unpack_from('<3f',quad,o) for o in (0x10,0x20,0x30,0x40)],'rgba':list(quad[0x80:0x90])}
  except Exception:payload=None
  assert payload is not None and all(math.isfinite(v) for point in payload['corners'] for v in point)
  assert all(payload['rgba'][i:i+3]==[110,100,75] and 0<=payload['rgba'][i+3]<=150 for i in (0,4,8,12))
  draws.append((args,hex(matrix),payload))
  uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
hook=m.uc.hook_add(UC_HOOK_CODE,capture)
for view in (1,4,10,11,12,13,14,15):
 scene=0x1011000+(view-10)*0x1000 if view>=10 else 0x1050000+(view-1)*0x1000
 if view<10:
  m.write(scene+4,view);m.write(scene+0x40,0x100b000)
 m.write(0x7355b4,model);calls.clear();draws.clear()
 try:
  m.run(0x4ae7d0,(env,scene))
  print('VIEW',view,'MODE',m.integer(env+0x1338),'FLARE_CALLS',len(calls),'DRAWS',draws)
 except Exception as error:
  print('UNRESOLVED_VIEW',view,'EIP',hex(m.uc.reg_read(UC_X86_REG_EIP)),'ERROR',str(error));raise
m.uc.hook_del(hook)
print('BOUNDARY: native all-view planes/visibility and local quad; near/far and global quality/texture allocation supplied')
