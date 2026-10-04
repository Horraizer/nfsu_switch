"""Execute the original FEng font color pack; no retail writes or devices.

Requires Unicorn. This is a bounded native slice with supplied BGRA inputs,
color pointer and scratch stack, not a complete original rendering process.
"""
from pathlib import Path
import argparse,hashlib,struct,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP
parser=argparse.ArgumentParser(description='Execute bounded original FEng color packing')
parser.add_argument('retail_root',type=Path)
exe=(parser.parse_args().retail_root/'speed.exe').read_bytes()
if hashlib.sha256(exe).hexdigest()!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
 raise SystemExit('original executable identity mismatch')
image=Image(exe);image.sections=[(a,s,r) for a,s,r,n in image.sections]
for color in [(242,233,207,255),(255,255,255,255),(1,3,5,7),(128,64,32,128)]:
 m=Machine(image);pointer=0x1001000;m.uc.mem_write(pointer,struct.pack('<4I',*color))
 for reg,value in [(UC_X86_REG_EAX,color[0]),(UC_X86_REG_EDX,color[2]),(UC_X86_REG_EDI,pointer),(UC_X86_REG_ESP,m.stack)]:m.uc.reg_write(reg,value)
 # Original 004F4956..004F4998 packs local A/B/G/R after signed /2.\n # 0040BEF0 later converts that local RGBA byte layout to D3D ARGB.
 m.uc.emu_start(0x4f4956,0x4f4999,count=100)
 packed=m.uc.reg_read(UC_X86_REG_ECX)
 expected=((color[3]//2)<<24)|((color[0]//2)<<16)|((color[1]//2)<<8)|(color[2]//2)
 if packed!=expected:raise RuntimeError('original half-channel color mismatch: %08X != %08X' % (packed,expected))
 print('NATIVE_COLOR',color,'%08X'%packed)
print('PASS: original FEng half-channel color pack')