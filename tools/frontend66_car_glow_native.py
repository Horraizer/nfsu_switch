"""Bounded original car flare descriptor and main CView witness.
Pass --retail-root as for frontend66_flare_native.py; no original writes.
Marker and texture allocation are explicit supplied boundaries.
"""
import sys,runpy,contextlib,io,struct
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
with contextlib.redirect_stdout(io.StringIO()):base=runpy.run_path('tools/frontend66_flare_native.py')
m=base['m'];scene=0x1050000;matrix=0x1090000;node=0x1091000
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
m.uc.mem_write(matrix,base['base']['identity']);m.uc.mem_write(matrix+56,struct.pack('<f',.20027998089790344))
for i in range(3):m.write(0x72fa14+i*4,base['texture']+i*0x100)
quad=[];inputs=[]
def capture(uc,at,size,data):
 sp=uc.reg_read(UC_X86_REG_ESP)
 if at==0x5443f0:
  words=[m.integer(sp+4+i*4) for i in range(11)]
  inputs.append((hex(uc.reg_read(UC_X86_REG_ESI)),[struct.unpack('<f',struct.pack('<I',x))[0] for x in words], [struct.unpack('<4f',uc.mem_read(ptr,16)) for ptr in words[1:5]]))
 if at==0x40c300:
  p=uc.reg_read(UC_X86_REG_EAX);q=bytes(uc.mem_read(p,0x94))
  quad.append(([struct.unpack_from('<3f',q,i) for i in (16,32,48,64)],q[128:144].hex()))
  uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
hook=m.uc.hook_add(UC_HOOK_CODE,capture)
for kind in (0,1):
 m.uc.mem_write(node+12,bytes((1,kind,0,0)))
 m.uc.mem_write(node+16,struct.pack('<3f',1.5,-.6,.5));m.uc.mem_write(node+28,bytes(4));m.uc.mem_write(node+32,struct.pack('<3f',1,0,0))
 quad.clear();inputs.clear()
 m.run(0x5447c0,(scene,node,0x3f800000,0,0,0),{UC_X86_REG_EAX:matrix})
 print('KIND',kind,'INPUTS',inputs,'QUADS',quad)
m.uc.hook_del(hook)
assert len(quad)==1
print('PASS: genuine original car sprite execution')
print('BOUNDARY: original car sprite types; supplied point/normal, native main CView/frustum and root placement')
