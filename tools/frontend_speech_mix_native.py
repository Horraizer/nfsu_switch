"""Authenticated readonly native FE music/speech mix witness. Requires Unicorn.
Fake mapped settings, parent and clock are explicit inputs; no audio IO.
51C8CE and4C5593 request profile9.53CAAF consumes settings music+35CC.
"""
from pathlib import Path
import sys,struct,hashlib,json
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
import argparse
parser=argparse.ArgumentParser(description='Execute original menu and speech music mix targets')
parser.add_argument('retail_root',type=Path);args=parser.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections];m=Machine(im);m.uc.mem_map(0,0x1000)
settings,parent,clock=0x1001000,0x1006000,0x1007000
m.run(0x5364e0,(settings,));assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
m.write(0x7356a8,parent);m.write(0x7356ac,settings);m.write(parent+0xa0,clock);m.write(clock+8,0)
m.run(0x536870,(0xbf800000,),{UC_X86_REG_ESI:8,UC_X86_REG_EDI:settings});assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
assert m.integer(settings+0x35c8)==26500
print('NORMAL_TARGET',m.integer(settings+0x35c8))
m.run(0x536870,(0xbf800000,),{UC_X86_REG_ESI:9,UC_X86_REG_EDI:settings});assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
assert m.integer(settings+0x35c8)==6300
print('SPEECH_TARGET',m.integer(settings+0x35c8))
print('ATTACK_RELEASE',struct.unpack('<2f',m.uc.mem_read(0x744078,8)))
assert struct.unpack('<2f',m.uc.mem_read(0x744078,8))==(.25,2.)
print('PASS original5364E0 initialization and536870 profile8/9 music target setters; no device or game IO')
