"""Read-only bounded original preset and sponsor witness.
Requires Unicorn. Stock initialization is an explicit zero-slot precondition,
with the native car index retained; original 0x4B26F0 and 0x57EA80 execute all
preset lookups and installation rules. A bounded host formatter implements
only the observed %s_WIDE%d_ call. This is not original whole-game startup.
Original 0x56C3D0 subsequently computes panel sponsor texture substitutions.
No game process, input asset, device, save or original file is modified.
"""
from pathlib import Path
import sys,struct,hashlib,json
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image,chunks,jdlz,name_hash,preset_records
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
import argparse
parser=argparse.ArgumentParser(description='Bounded original FE_SKYLINE preset, enable and sponsor witness')
parser.add_argument('retail_root',type=Path)
root=parser.parse_args().retail_root
exe=(root/'speed.exe').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
global_b=(root/'Global/GLOBALB.BUN').read_bytes()
front_raw=(root/'FrontEnd/FrontB.lzc').read_bytes()
assert hashlib.sha256(global_b).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
assert hashlib.sha256(front_raw).hexdigest()=='76f368d5e5e30f96a8182bf457dba4491395e075c3f336d86a32012b56e16f90'
front=jdlz(front_raw)
row=preset_records(front)[name_hash('FE_SKYLINE')]
def payload(tag):
 matches=[(a,b) for t,a,b in chunks(global_b) if t==tag]
 assert len(matches)==1
 a,b=matches[0];start=a+8;start=(start+15)&~15 if tag==0x34600 else start;return global_b[start:b]
records=bytearray(payload(0x34604));attributes=payload(0x34605);types=bytearray(payload(0x34600))
im=Image(exe);im.sections=[(a,s,r) for a,s,r,n in im.sections];m=Machine(im)
m.uc.mem_map(0x2000000,0x400000)
record_base,attr_base,type_base,node,ride,preset=0x2000000,0x2100000,0x2200000,0x2300000,0x2310000,0x2320000
for k in range(len(records)//48):
 for index,count in ((20,28),(24,30)):
  count_value=struct.unpack_from('<h',records,k*48+count)[0]
  old=struct.unpack_from('<I',records,k*48+index)[0]
  struct.pack_into('<I',records,k*48+index,attr_base+old*8 if count_value else 0)
m.uc.mem_write(record_base,bytes(records));m.uc.mem_write(attr_base,attributes)
assert len(types)==35*0xc90
for k in range(35):
 name=bytes(types[k*0xc90:k*0xc90+32]).split(b'\0')[0].decode('ascii')
 struct.pack_into('<I',types,k*0xc90+0xd0,name_hash(name,False))
m.uc.mem_write(type_base,bytes(types));m.write(0x734588,type_base)
m.write(0x73ad40,node);m.write(node,0x73ad40);m.write(node+12,record_base);m.write(node+16,len(records)//48)
m.uc.mem_write(preset,front[row['offset']:row['offset']+0x408]);m.write(0x748844,preset);m.write(preset,0x748844)
# Execute all original preset lookup branches. Stock initialization is captured
# as an explicit boundary; slot19 is deliberately untouched by this routine.
calls=[]
def hook(uc,a,size,data):
 if a==0x57d160:
  calls.append(dict(stock_car=uc.reg_read(UC_X86_REG_EAX)));m.write(ride,uc.reg_read(UC_X86_REG_EAX))
  esp=uc.reg_read(UC_X86_REG_ESP)
  uc.reg_write(UC_X86_REG_EIP,m.integer(esp));uc.reg_write(UC_X86_REG_ESP,esp+16)
 if a==0x565da0:
  esp=uc.reg_read(UC_X86_REG_ESP);fmt=m.text(m.integer(esp+4))
  if fmt!='%s_WIDE%d_':raise RuntimeError('unexpected native format '+fmt)
  text=fmt%(m.text(m.integer(esp+8)),m.integer(esp+12))
  uc.mem_write(uc.reg_read(UC_X86_REG_ESI),text.encode('ascii')+b'\0')
  uc.reg_write(UC_X86_REG_EAX,len(text));uc.reg_write(UC_X86_REG_EIP,m.integer(esp));uc.reg_write(UC_X86_REG_ESP,esp+4)
 if a==0:raise RuntimeError('unexpected null native call')
m.uc.hook_add(UC_HOOK_CODE,hook)
m.write(m.stack,m.stop);m.write(m.stack+4,ride);m.uc.reg_write(UC_X86_REG_ESP,m.stack);m.uc.reg_write(UC_X86_REG_EAX,name_hash('FE_SKYLINE'))
m.uc.emu_start(0x4b26f0,m.stop,count=20000000)
assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
values=[m.integer(ride+0x44c+k*4) for k in range(120)]
assert calls==[dict(stock_car=19)]
m.run(0x56c3d0,(0x2330000,ride))
assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
print('DECALS',[[hex(m.integer(0x2330000+0xab0+panel*0x80+k*16)) for k in range(8)] for panel in range(7)])
print(json.dumps(dict(calls=calls,enabled=list(m.uc.mem_read(ride+0x6a4,120)),parts=[(v-record_base)//48 if v else None for v in values])))
