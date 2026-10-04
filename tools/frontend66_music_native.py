"""Bounded native witness for issue66's fresh-profile FE playlist.

Requires Unicorn. Executes the authenticated PE slices in memory; no original
process, game files, devices or saves are modified. The selector is stopped
before external voice/file IO. The initialized registers and fake caller/voice
objects are explicit preconditions, not a claim to execute the entire startup.
"""
from pathlib import Path
import sys,struct,json
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
import argparse, hashlib
parser=argparse.ArgumentParser(description="Execute bounded original fresh-profile menu song selection")
parser.add_argument("retail_root",type=Path)
root=parser.parse_args().retail_root
exe=(root/"speed.exe").read_bytes()
if hashlib.sha256(exe).hexdigest()!= "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7":
 raise SystemExit("original executable identity mismatch")
image=Image(exe)
original_sections=image.sections
image.sections=[(a,s,r) for a,s,r,n in image.sections]
m=Machine(image)
# Run the original initializer's song loop, including both category compares.
for reg,value in [(UC_X86_REG_EAX,0x7588c4),(UC_X86_REG_ECX,0),(UC_X86_REG_EDX,1)]: m.uc.reg_write(reg,value)
m.uc.emu_start(0x4aa881,0x4aa959,count=20000)
rows=[(m.integer(0x758b58+i*8),bytes(m.uc.mem_read(0x758b5c+i*8,1))[0]) for i in range(26)]
print('PROFILE',rows)
m.run(0x520e90,(0,0,1),{UC_X86_REG_EBX:0})
print('MASKS',hex(m.integer(0x758930)),hex(m.integer(0x758934)))
parent,voice,obj,stream=0x1001000,0x1002000,0x1003000,0x1004000
m.write(obj+4,parent);m.write(0x77a920,3);m.write(0x7356a8,parent);m.write(m.stack+0x14,stream)
for reg,value in [(UC_X86_REG_EAX,3),(UC_X86_REG_EDI,obj),(UC_X86_REG_EBX,voice),(UC_X86_REG_EBP,stream)]: m.uc.reg_write(reg,value)
m.uc.emu_start(0x53c4dd,0x53c671,count=20000)
print('SELECTION',dict(bank_id=m.integer(voice+0x54),song_index=m.integer(parent+0xfc),cursor=m.integer(parent+0xf8)))
assert rows==[(i,2 if i<19 else 1) for i in range(26)]
assert m.integer(voice+0x54)==20 and m.integer(parent+0xfc)==19

assert m.integer(0x758930)==0x07f00000
assert m.integer(0x758934)==0x000ffffe
image.sections=original_sections
metadata=[tuple(image.read(struct.unpack("<I",image.read(0x6f4d08+i*16+j*4,4))[0],256).split(b"\0",1)[0].decode("ascii") for j in range(4)) for i in range(26)]
assert metadata[19]==('"Get Low"',"Lil Jon & the Eastside Boyz","Kings of Crunk","FE")
assert all(v[3]==("IG" if i<19 else "FE") for i,v in enumerate(metadata))
print("FE_METADATA",json.dumps(metadata[19:]))
print("PASS native fresh-profile FE mask, selection and metadata")
