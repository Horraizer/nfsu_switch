from pathlib import Path
import sys,hashlib,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDI,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
root=parser.parse_args().retail_root
b=(root/'speed.exe').read_bytes()
assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(b);im.sections=[(a,n,r) for a,n,r,_ in im.sections]
for tick,hide_started in [(20799,0),(20800,0),(20801,0),(24799,1),(24800,1),(24801,1)]:
 m=Machine(im);obj=0x1001000;m.write(obj+0x40,0);m.uc.mem_write(obj+0x44,bytes((hide_started,1)));m.write(0x73ad3c,tick);calls=[]
 def hook(uc,at,size,_):
  if at not in [0x4d7610,0x4f63a0]:return
  calls.append('%08X'%at);sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 m.uc.hook_add(UC_HOOK_CODE,hook);m.run(0x4d74c0,(0xc98356ba,0,0,0),{UC_X86_REG_ECX:obj})
 expected_calls=[] if tick in (20799,24799) else ['004D7610'] if not hide_started else ['004F63A0']
 assert calls==expected_calls
 assert m.uc.mem_read(obj+0x45,1)[0]==(0 if hide_started and tick>=24800 else 1)
 print(tick,'hide_started',hide_started,'flags',bytes(m.uc.mem_read(obj+0x44,2)).hex(),'calls',calls)
print('Native callback executed; Hide-selection and package-close calls are explicit boundary stubs, not whole retail startup')

# Execute the original song metadata dispatcher/controller with authenticated
# table pointers. Lookup and Show are bounded stubs; text target/hash and
# incoming strings are captured from actual 4D76A0 -> 4D7580 instructions.
for song_index in (0,19):
 m=Machine(im);component=0x1001000;package=0x1002000
 m.write(package+0x14,component);m.write(component+0xc,0x6c2b28)
 m.uc.mem_write(0x735e1c,b'\x01');calls=[]
 def text_hook(uc,at,size,_):
  sp=uc.reg_read(UC_X86_REG_ESP)
  if at==0x4f3f90:
   uc.reg_write(UC_X86_REG_EAX,package)
   uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+8)
  elif at in (0x4f6850,0x4f6bd0):
   if at==0x4f6850:
    calls.append((uc.reg_read(UC_X86_REG_EDI),m.text(m.integer(sp+4))))
   uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 m.uc.hook_add(UC_HOOK_CODE,text_hook)
 args=tuple(m.integer(0x6f4d08+song_index*16+j*4) for j in range(3))+(0x6b9172,)
 title,artist,album=[m.text(p) for p in args[:3]]
 assert title.startswith('"') and title.endswith('"')
 m.run(0x4d76a0,args)
 assert calls==[(0xf5387816,artist),(0x48097852,album),(0x11db9e17,title)]
 print('CHYRON_TEXT',song_index,calls)
print('PASS native TrackTitle receives artist, ArtistName receives quoted title, AlbumTitle receives album')

# Execute genuine nonloop arithmetic using authored Chyron child FI/Kd bytes.
# Script phase is supplied; no native package dispatch is claimed here.
import struct
f=(root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(f).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
for objpos in (0x2392d0,0x239d50,0x23aa6c):
 n=struct.unpack_from('<I',f,objpos+12)[0]
 outer=struct.unpack_from('<I',f,objpos+4)[0]
 data=f[objpos+16+n:objpos+8+outer];q=0
 while q+8<=len(data):
  length=struct.unpack_from('<I',data,q+4)[0];chunk=data[q+8:q+8+length];q+=8+length
  if data[q-length-8:q-length-4]!=b'Scrp':continue
  fields=[];r=0
  while r<len(chunk):
   size=struct.unpack_from('<H',chunk,r+2)[0];fields.append((chunk[r:r+2],chunk[r+4:r+4+size]));r+=4+size
  name=next(v.split(bytes(1))[0] for t,v in fields if t==b'Sn')
  if name not in (b'Show',b'Hide'):continue
  fi=None;target=0
  for tag,value in fields:
   if tag==b'FI':fi=value
   if tag==b'To':target=struct.unpack('<I',value)[0]
   if tag!=b'Kd':continue
   stride=16 if fi[0]==4 else 20
   for tick in (0,40,80,480,600,840,870,900):
    m=Machine(im);frame=0x1001000;out=0x1002000
    m.uc.mem_write(frame,fi);m.uc.mem_write(frame+0x18,value[4:stride])
    keys=[value[i:i+stride] for i in range(stride,len(value),stride)]
    nodes=[0x1003000+i*64 for i in range(len(keys))]
    m.write(frame+0x30,nodes[0] if nodes else 0);m.write(frame+0x34,nodes[-1] if nodes else 0)
    for i,node in enumerate(nodes):
     m.write(node+4,nodes[i+1] if i+1<len(nodes) else 0);m.write(node+8,nodes[i-1] if i else 0);m.uc.mem_write(node+12,keys[i])
    m.run(0x4fc100,(frame,tick,out))
    result=struct.unpack('<4i' if fi[0]==6 else '<3f',m.uc.mem_read(out,16 if fi[0]==6 else 12))
    if objpos==0x2392d0 and target==0:
     expected={0:0,40:0,80:0,480:0,600:0,840:0,870:110,900:220} if name==b'Show' else {0:255,40:128,80:0,480:0,600:0,840:0,870:0,900:0}
     assert result==(242,233,207,expected[tick])
    if objpos==0x23aa6c and name==b'Show' and target==7 and tick==900:
     assert fi[4:]==bytes(4) and result==(182.,8.,30.)
    print('CHYRON_KEY',hex(objpos),name.decode(),target,fi.hex(),tick,result)
print('PASS genuine Chyron child Show/Hide text keys and zero-FI-duration position at supplied phases')
