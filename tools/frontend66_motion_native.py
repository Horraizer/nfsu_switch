"""Read-only bounded original frontend wheel phase and spin witnesses.
Requires Unicorn. Supplies road travel, wheel-0 radius and zero car switch
travel as explicit inputs; this does not emulate whole-game startup.
"""
from pathlib import Path
import sys,struct,hashlib,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
parser=argparse.ArgumentParser()
parser.add_argument('retail_root',type=Path)
raw=(parser.parse_args().retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
for phase in (0,8192,16384,7253):
 m=Machine(im);m.write(0x6d4acc,0x5ac6a2);out=0x1001000;m.uc.mem_write(out,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
 try:
  m.run(0x40ba40,(out,out),{UC_X86_REG_EDX:phase});print(phase,struct.unpack('<16f',m.uc.mem_read(out,64)))
 except Exception:raise
for delta,radius,expected in ((.231441572,.31,7787),(.229,.31,7705),(1.,.31,33646),(13.6265792847/60,.32659998536109924,7253)):
 m=Machine(im);main=0x1001000;ride=0x1002000;stack=0x1003000
 m.uc.mem_write(0x746dd8,struct.pack('<f',delta));m.uc.mem_write(0x6d4acc,struct.pack('<I',0x5ac6a2));m.uc.mem_write(main+0x104,struct.pack('<f',0));m.uc.mem_write(main+0x108,struct.pack('<H',0));m.uc.mem_write(ride+0x80,struct.pack('<f',radius));m.uc.mem_write(0x73ad38,struct.pack('<f',1/60));m.uc.mem_write(stack+0x10,struct.pack('<I',ride))
 try:
  m.uc.reg_write(UC_X86_REG_ESI,main);m.uc.reg_write(UC_X86_REG_ESP,stack);m.uc.emu_start(0x4b18f7,0x4b1939,count=500)
  actual=struct.unpack('<H',m.uc.mem_read(main+0x108,2))[0]
  assert actual==expected,(actual,expected)
  print('PHASE',delta,radius,actual)
 except Exception:raise

print("PASS: original frontend wheel phase slices and 40BA40 spin matrices")
