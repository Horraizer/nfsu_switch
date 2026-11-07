"""Bounded original main-menu saved camera and primary projection oracle.

Executes authenticated retail code. Explicit boundaries are zero-initialized
camera storage, original scalar CPU dispatch, a supplied viewport, and clipping
planes. No camera arithmetic, trigonometry, matrix or projection call is stubbed.
The projection slice stops before scene submission; only focal scales are claimed.
"""
from pathlib import Path
import argparse,hashlib,struct,sys
sys.dont_write_bytecode=True
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
from unicorn import UC_HOOK_CODE
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
raw=(parser.parse_args().retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
m=Machine(im);camera=0x1001000;stack=0x1005000
# Authenticated constructor's saved preset tail; EAX is zero from earlier ctor.
m.uc.reg_write(UC_X86_REG_EAX,0);m.uc.reg_write(UC_X86_REG_ESI,camera);m.uc.reg_write(UC_X86_REG_ESP,stack)
m.uc.emu_start(0x486ecd,0x487069,count=200)
# Genuine mode0 entry setter followed by actual update selector; stop before matrix conversion.
m.run(0x487ba0,(),{UC_X86_REG_ESI:camera,UC_X86_REG_EAX:0})
assert m.integer(camera+0x2c)==1 and m.integer(camera+0x30)==0
m.write(0x735f0c,0)
m.uc.reg_write(UC_X86_REG_ECX,camera);m.uc.reg_write(UC_X86_REG_ESP,stack)
m.uc.emu_start(0x4870c0,0x48752c,count=200)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x48752c
selected=m.uc.reg_read(UC_X86_REG_EAX);assert selected==0x7788e0
print('MODE0_SELECTED_CAMERA',hex(selected),struct.unpack('<16f',m.uc.mem_read(selected,64)))
# Original scalar dispatch table. Explicit CPU boundary avoids registry/OS feature discovery.
m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)));m.write(0x6d4d00,0)
out=0x1003000
m.run(0x487680,(out,),{UC_X86_REG_EAX:selected})
print('MODE0_NATIVE_CAMERA_MATRIX',struct.unpack('<16f',m.uc.mem_read(out,64)))
print('PASS: native mode0 camera conversion with original scalar math; no camera arithmetic stubs')

view=0x1006000;scene=0x1007000;viewport=0x1008000;projection=0x1009000;stack=0x100a000
m.write(camera+0x1c,view)
for reg,value in ((UC_X86_REG_EDI,selected),(UC_X86_REG_ESI,camera),(UC_X86_REG_ESP,stack)):m.uc.reg_write(reg,value)
m.uc.emu_start(0x48753d,0x487576,count=100)
fov=struct.unpack('<H',m.uc.mem_read(view+0xc4,2))[0];print('NATIVE_FOV_WORD',fov)
m.write(scene+0x40,view);m.write(scene+0x58,viewport);m.write(scene+4,1)
m.uc.mem_write(view+0xbc,struct.pack('<ff',.05,300.))
for width,height in ((1280,720),(640,480)):
 m.write(viewport+0x14,width);m.write(viewport+0x18,height)
 def stop(uc,a,s,d):
  if a==0x40df04:uc.emu_stop()
 hook=m.uc.hook_add(UC_HOOK_CODE,stop)
 m.uc.reg_write(UC_X86_REG_ESI,projection);m.uc.reg_write(UC_X86_REG_ESP,stack);m.uc.mem_write(stack,struct.pack('<5I',m.stop,scene,0,0,0));m.uc.emu_start(0x40dd00,m.stop,count=10000)
 assert m.uc.reg_read(UC_X86_REG_EIP)==0x40df04
 matrix=struct.unpack('<16f',m.uc.mem_read(projection+0x40,64));print('NATIVE_PROJECTION',width,height,matrix)
 m.uc.hook_del(hook)
print('PASS: original camera setter and primary-view projection consumer')

