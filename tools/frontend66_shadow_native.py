"""Read-only FE_SKYLINE shadow footprint/grid/alpha witness.
Requires Unicorn. Reuses authenticated native preset lookup witness, then
executes original bounds reducer, inflation and FE grid slices. Solid loader
inputs are authenticated file headers; this is a bounded main-menu witness.
"""
from pathlib import Path
import sys,struct,runpy,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
state=runpy.run_path(str(Path(__file__).with_name('frontend66_preset_native.py')))
root,im=state['root'],state['im'];records,parts=state['records'],state['values'];base=state['record_base']
from hud254_native import Machine
from unicorn.x86_const import *
from event0_trace import chunks
geometry=(root/'Cars/SKYLINE/GEOMETRY.BIN').read_bytes()
assert hashlib.sha256(geometry).hexdigest()=='64da8a1f29ac7c94832675c1fd164b2c0236104ad6b9e97010afd873c12f1f12'
entries={}
for tag,a,end in chunks(geometry):
 if tag==0x134004:
  for at in range(a+8,end,20):
   h,offset,size=struct.unpack_from('<III',geometry,at);entries[h]=(offset,size)
m=Machine(im);render=0x1001000;wrapper=0x1002000;frame=0x1003000;stack=0x1004000;source=0x1006000
m.uc.mem_write(render+0x10,struct.pack('<3f',*[3.4028234663852886e38]*3));m.uc.mem_write(render+0x20,struct.pack('<3f',*[-3.4028234663852886e38]*3))
for slot in range(12):
 index=(parts[slot]-base)//48;model=struct.unpack_from('<I',records,index*48+32)[0]
 low=high=(0.,0.,0.)
 # Original constructor's A lookup falls back to B; if both are absent,
 # its solid pointer is null and bounds are zero (0x56AB37).
 if model and model not in entries:model=struct.unpack_from('<I',records,index*48+36)[0]
 if model not in entries:model=0
 if model:
  offset,size=entries[model];cursor=offset+8;found=False
  while cursor<offset+size:
   tag,n=struct.unpack_from('<II',geometry,cursor);payload=cursor+8
   assert payload+n<=offset+size
   if tag==0x134011:
    aligned=(payload+15)&~15;assert payload+n>=aligned+0x3c
    low=struct.unpack_from('<3f',geometry,aligned+0x20);high=struct.unpack_from('<3f',geometry,aligned+0x30);found=True;break
   cursor=payload+n
  assert found
 m.uc.mem_write(source,struct.pack('<3f',*low));m.uc.mem_write(source+0x10,struct.pack('<3f',*high))
 m.run(0x567890,(),{UC_X86_REG_ESI:render+0x20,UC_X86_REG_EDX:render+0x10,UC_X86_REG_EAX:source+0x10,UC_X86_REG_ECX:source})
 assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
m.write(wrapper+8,render);m.write(frame-0x28,0)
for reg,value in ((UC_X86_REG_EBX,wrapper),(UC_X86_REG_EBP,frame),(UC_X86_REG_ESP,stack)):m.uc.reg_write(reg,value)
m.uc.emu_start(0x56ab99,0x56abef,count=100)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x56abef
print('NATIVE_FE_FOOTPRINT',struct.unpack('<3f',m.uc.mem_read(render+0x10,12)),struct.unpack('<3f',m.uc.mem_read(render+0x20,12)))
stack=0x1008000;matrix=0x100a000;origin=0x100b000
m.uc.mem_write(matrix,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,.20027998089790344,1));m.uc.mem_write(stack+0x10,struct.pack('<f',0));m.write(stack+0xa8,1);m.write(stack+0x24,render+0x20);m.write(stack+0x2c,render+0x10);m.write(stack+0x30,matrix);m.write(frame+0x10,origin);m.uc.mem_write(origin+8,struct.pack('<f',.20027998089790344));m.write(0x6d4ac4,0x5abdb5)
for reg,value in ((UC_X86_REG_ESP,stack),(UC_X86_REG_EBP,frame)):m.uc.reg_write(reg,value)
m.uc.emu_start(0x56eb33,0x56efd9,count=10000)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x56efd9
for i in range(16):print('NATIVE_FE_GRID',i,struct.unpack('<3f',m.uc.mem_read(stack+0x5b0+i*16,12)),struct.unpack('<2f',m.uc.mem_read(stack+0x4f0+i*8,8)))
for fade in (0.,.5,1.):
 m.uc.mem_write(stack+0x10,struct.pack('<f',fade));m.write(frame+8,origin)
 for reg,value in ((UC_X86_REG_ESP,stack),(UC_X86_REG_EBP,frame),(UC_X86_REG_EBX,matrix),(UC_X86_REG_EDI,1)):m.uc.reg_write(reg,value)
 m.uc.emu_start(0x56efd9,0x56f055,count=1000)
 assert m.uc.reg_read(UC_X86_REG_EIP)==0x56f055
 assert m.uc.reg_read(UC_X86_REG_EAX)>>24==92
print('PASS: original FE_SKYLINE shadow footprint, all16 grid vertices and fixed alpha92')
