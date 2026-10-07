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
# Observe the actual main backing's type9 geometry and four packed colors.
# Allocation, texture lookup and COM submission use the existing bounded
# Renderer harness. These stubs do not supply the emitted corner colors.
from event0_trace import jdlz
from hud254_presentation_native import Renderer
front=jdlz((parser.parse_args().retail_root/'FrontEnd/FrontB.lzc').read_bytes())
if hashlib.sha256(front).hexdigest()!='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77':
 raise SystemExit('original frontend identity mismatch')
offset=0xbec98c;length=struct.unpack_from('<I',front,offset+12)[0]
fng=jdlz(front[offset:offset+length]);found=0
import re
for match in re.finditer(rb'FOb\xea',fng):
 at=match.start()+16;end=at+struct.unpack_from('<I',fng,at-4)[0];fields={}
 while at<end:
  tag=fng[at:at+2];size=struct.unpack_from('<H',fng,at+2)[0]
  fields[tag]=fng[at+4:at+4+size];at+=4+size
 if fields.get(b'On',b'').split(b'\0')[0]!=b'MenuScreen' or struct.unpack('<I',fields[b'Ot'])[0]!=9:continue
 obj={'fields':[{'tag':tag.decode('ascii'),'raw':value.hex()} for tag,value in fields.items()],
      'default_resource':{'name':'White16x16'}}
 native=Renderer(image);native.object_fields(obj,'')
 native.uc.mem_write(native.texture+0x44,struct.pack('<hh',16,16))
 native.run(0x4f2280,(native.context,native.obj),{UC_X86_REG_EAX:0})
 quad=native.submissions[0]['quads'][0]
 if quad['colors']!=['7A604000','7A604000','7A604000','752C230B']:
  raise RuntimeError('original main backing corner color mismatch')
 positions=quad['positions']
 if not(positions[0][0]<positions[1][0] and positions[1][1]<positions[2][1] and positions[2][0]>positions[3][0]):
  raise RuntimeError('original TL/TR/BR/BL corner ordering mismatch')
 print('NATIVE_MAIN_BACKING',quad['colors'],'TL/TR/BR/BL');found+=1
if found!=1:raise RuntimeError('original main backing absent or ambiguous')
print('PASS: original FEng half-channel color pack and main backing gradient')