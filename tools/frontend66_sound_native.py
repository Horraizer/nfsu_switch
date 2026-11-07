"""Bounded native FE default gain/pitch witness. Requires Unicorn.
The original settings constructor executes with a dummy mapped SEH chain.
The common descriptor's type4 is read from the authenticated executable;
original 0x52E3F0 initializes its unattenuated gain. Only the original gain
arithmetic slice of 0x523120 is then executed, stopping before event allocation.
No device, original process, asset, save or runtime preferences are modified.
This proves the default gain/pitch boundary, not the event voice allocator.
"""
from pathlib import Path
import sys,struct,hashlib,argparse,json
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
parser=argparse.ArgumentParser(description='Original FE common cue default gain')
parser.add_argument('retail_root',type=Path)
root=parser.parse_args().retail_root
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw)
assert struct.unpack('<I',image.read(0x6f43b0+0x14,4))[0]==4
image.sections=[(a,s,r) for a,s,r,n in image.sections]
m=Machine(image);m.uc.mem_map(0,0x1000)
settings,obj=0x1001000,0x1006000
m.run(0x5364e0,(settings,))
assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
master=m.integer(settings+0x3a8c)
m.run(0x52e3f0,(),{UC_X86_REG_EAX:obj+0x20})
assert m.uc.reg_read(UC_X86_REG_EIP)==m.stop
unattenuated=m.integer(obj+0x20+0xa8)
m.write(obj+0x70,unattenuated);m.write(0x7356ac,settings)
m.uc.reg_write(UC_X86_REG_EBP,obj)
m.uc.emu_start(0x523171,0x523192,count=1000)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x523192
gain=m.uc.reg_read(UC_X86_REG_ESI)
payload=0x1009000
m.uc.reg_write(UC_X86_REG_EDI,payload);m.uc.reg_write(UC_X86_REG_EAX,gain)
m.uc.emu_start(0x523208,0x523219,count=100)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x523219
print(json.dumps(dict(master_q15=master,unattenuated_q15=unattenuated,
    cue_gain_q15=m.integer(payload+8),native_pitch_unity=m.integer(payload+12),
    event_flags=m.integer(payload+16))))
