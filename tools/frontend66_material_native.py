"""Read-only bounded original frontend material binding witnesses.
Requires Unicorn. The missing-texture case supplies an explicit empty bucket;
it proves native lookup/draw policy, not whole-game asset availability. The
skin case supplies the existing profile target and executes real source/target
assignment slices; it does not reconstruct a second skin compositor.
"""
from pathlib import Path
import sys,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))

from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
import argparse
parser=argparse.ArgumentParser(description='Bounded original frontend material witnesses')
parser.add_argument('retail_root',type=Path)
raw=(parser.parse_args().retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections];m=Machine(im)
print('EMPTY_NAME',m.text(0x6cc00c))
m.run(0x689ad0,())
assert m.integer(0x78a4e0)==0x66994f4a
# Explicit boundary: the supplied native bucket has no matching texture.
# This tests the real missing-binding policy, not whole-game asset loading.
texture,mesh,slots,material=0x1001000,0x1002000,0x1003000,0x1004000
m.write(texture+0x24,m.integer(0x78a4e0));m.write(0x73551c,texture)
m.write(0x740418,0x740418);m.write(0x74041c,0x740418)
m.uc.mem_write(mesh+0x19,b'\x01');m.write(mesh+0x2c,slots);m.write(slots,0xc2456b43)
m.run(0x53f9b0,(),{UC_X86_REG_EBX:mesh})
assert m.integer(slots+4)==texture
m.uc.mem_write(material+0x1c,b'\x00');m.uc.reg_write(UC_X86_REG_EDI,mesh);m.uc.reg_write(UC_X86_REG_ESI,material)
m.uc.emu_start(0x40c9f8,0x40ca60,count=10)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x40ca60
print('PASS native missingC2456B43 -> DEFAULTALPHA -> original40C910 draw skipped at40CA60')

m=Machine(im)
car,ride=0x1001000,0x1004000;plan=car+0x40
m.uc.reg_write(UC_X86_REG_EBX,plan);m.uc.emu_start(0x580ba6,0x580bca,count=200)
assert m.integer(plan+0x3d0)==0xdda11455
# Existing native profile target is a boundary input; no second compositor.
m.write(ride+0x444,0x530b82b0);m.uc.reg_write(UC_X86_REG_ESI,ride);m.uc.reg_write(UC_X86_REG_EDI,0)
m.uc.emu_start(0x580c31,0x580c79,count=40)
assert m.integer(plan+0x418)==0x530b82b0 and m.integer(plan+0x420)==0x530b82b0
m.uc.reg_write(UC_X86_REG_ESI,car);m.uc.emu_start(0x56c31f,0x56c38f,count=80)
assert m.integer(car+0x710)==0x530b82b0 and m.integer(car+0x730)==0x530b82b0
print('PASS native GLOBAL_SKIN1 source shares body targetDUMMY_SKIN1 through580C37..580C73 and56C375..56C389')
