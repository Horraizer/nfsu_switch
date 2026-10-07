"""Bounded FE startup harmonic and actual car LocalEyePos native witness.

Authenticates retail code; executes original mode0 camera, CView publication,
reflection getter/basis and full542E40/544390 consumers. Zero-BSS harmonic
flags/reflection origin and empty light lists are explicit supplied assumptions,
not recovered retail profile defaults. Actual moving road lights remain a
separate populated-state witness. No lighting/matrix arithmetic is stubbed.
"""
from pathlib import Path
import argparse,sys,struct,hashlib,runpy
sys.dont_write_bytecode=True
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--road-offset',type=float,default=None)
parser.add_argument('--trace-accumulation',action='store_true')
parser.add_argument('--wheel-quarter-turn',action='store_true')
args=parser.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
m=Machine(im)
m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)));m.write(0x6d4d00,0)
identity=struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
m.write(0x77a920,3)
# Explicit boundary assumptions: no race lights; road RGB zero and lists
# empty here. Actual road markers/pool are separately recovered, not omitted
# from the claimed populated-menu state (which this witness does not claim).
m.write(0x740a68,0x740a68);m.write(0x743c10,0x743c10)
camera=0x1006000
m.uc.reg_write(UC_X86_REG_EAX,0);m.uc.reg_write(UC_X86_REG_ESI,camera);m.uc.reg_write(UC_X86_REG_ESP,m.stack)
m.uc.emu_start(0x486ecd,0x487069,count=200)
m.run(0x487680,(0x1007000,),{UC_X86_REG_EAX:0x7788e0})
# Genuine reflection camera getter initializes its distinct cached default state.
m.run(0x487590,(),{UC_X86_REG_ESI:0x100a000})
print('NATIVE_REFLECTION_CAMERA_EYE',struct.unpack('<3f',m.uc.mem_read(0x100a000,12)))
# Stop after the actual FE reflection basis is published, before cube-view submission.
from unicorn import UC_HOOK_CODE
def basis_stop(uc,a,size,data):
 if a==0x40ae97:uc.emu_stop()
hook=m.uc.hook_add(UC_HOOK_CODE,basis_stop)
m.write(0x7364a0,3)
m.uc.reg_write(UC_X86_REG_ESP,m.stack);m.write(m.stack,m.stop)
m.uc.emu_start(0x5707b0,m.stop,count=20000)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x40ae97
print('NATIVE_FE_REFLECTION_BASIS',struct.unpack('<16f',m.uc.mem_read(0x78ea40,64)))
m.uc.hook_del(hook)# Original CView publication computes eye/target from the actual entry view matrix.
cv=0x100b000;scene=0x100c000;car_matrix=0x100d000;local=0x100e000
m.uc.mem_write(cv+0xb0,struct.pack('<f',3.190000057220459))
def view_stop(uc,a,size,data):
 if a==0x47d1db:uc.emu_stop()
hook=m.uc.hook_add(UC_HOOK_CODE,view_stop)
m.write(m.stack,m.stop);m.write(m.stack+4,cv);m.write(m.stack+8,0x1007000);m.write(m.stack+12,0)
m.uc.reg_write(UC_X86_REG_ESP,m.stack);m.uc.emu_start(0x47d070,m.stop,count=10000)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x47d1db
m.uc.hook_del(hook)
print('NATIVE_PRIMARY_CVIEW_EYE',struct.unpack('<3f',m.uc.mem_read(cv+0x40,12)))
m.write(scene+0x40,cv);m.uc.mem_write(car_matrix,identity);m.uc.mem_write(car_matrix+0x38,struct.pack('<f',.20027998089790344))
if args.road_offset is not None:
 saved_argv=sys.argv[:]
 sys.argv=[str(Path(__file__).with_name('frontend66_road_light_native.py')),'--retail-root',str(args.retail_root),'--road-offset',str(args.road_offset)]
 try:road=runpy.run_path(sys.argv[0])
 finally:sys.argv=saved_argv
 producer=road['m'];m.uc.mem_write(0x1001000,bytes(producer.uc.mem_read(0x1001000,0x5000)))
 m.write(0x743c10,0x100101c);m.write(0x743c14,0x100101c)
 print('BOUNDARY_ROAD_INPUTS: original pool; injected RGB17/39/71 height.125 attenuation.4/.03/.001 not defaults')
def accumulation(uc,at,size,data):
 if at==0x5416e0:
  sp=uc.reg_read(UC_X86_REG_ESP);color=uc.reg_read(UC_X86_REG_ECX)
  print('NATIVE_SH_ACCUMULATE',struct.unpack('<4f',uc.mem_read(color,16)),struct.unpack('<4f',uc.mem_read(sp+4,16)))
accumulation_hook=m.uc.hook_add(UC_HOOK_CODE,accumulation) if args.trace_accumulation else None
m.run(0x542e40,(0x1003000,car_matrix+0x30,0,0x78ea40),{UC_X86_REG_EAX:scene})
if accumulation_hook is not None:m.uc.hook_del(accumulation_hook)
m.run(0x544390,(0x78ea40,cv+0x40,scene,0x1003000),{UC_X86_REG_EAX:local,UC_X86_REG_ESI:car_matrix})
if args.wheel_quarter_turn:
 base_h=bytes(m.uc.mem_read(local+0x120,160))
 m.uc.mem_write(car_matrix,struct.pack('<16f',0,0,-1,0,0,1,0,0,1,0,0,0,0,0,.20027998089790344,1))
 m.run(0x544390,(0x78ea40,cv+0x40,scene,0x1003000),{UC_X86_REG_EAX:local,UC_X86_REG_ESI:car_matrix})
 rotated_h=bytes(m.uc.mem_read(local+0x120,160))
 assert base_h!=rotated_h
 def contract(h,n,c):
  terms=(1,n[0]*n[0],n[1]*n[1],n[2]*n[2],n[0]*n[1],n[2]*n[0],n[1]*n[2],n[0],n[1],n[2])
  return sum(h[i*4+c]*terms[i] for i in range(10))
 a=struct.unpack('<40f',base_h);b=struct.unpack('<40f',rotated_h)
 for n in ((.2,.3,.7),(1.,0,0),(0,0,1.),(-.4,.6,-.2)):
  transformed=(n[2],n[1],-n[0])
  assert max(abs(contract(a,transformed,c)-contract(b,n,c)) for c in range(4))<2e-6
 print('PASS native per-placement harmonic basis; unrotated block negative control differs')
print('NATIVE_FE_LOCAL_EYE',struct.unpack('<4f',m.uc.mem_read(local+0xc0,16)))
print('NATIVE_FE_LOCAL_HARMONIC',struct.unpack('<40f',m.uc.mem_read(local+0x120,160)))
assert tuple(struct.unpack('<2f',m.uc.mem_read(0x6b6348,8)))==(2.7799999713897705,)*2
print('PASS: bounded native FE startup record; unresolved startup/list assumptions remain explicit')
