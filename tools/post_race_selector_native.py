from pathlib import Path
import hashlib,sys,struct,json
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EBX,UC_X86_REG_EAX
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--root',required=True);args=parser.parse_args()
raw=(Path(args.root)/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);cases=[]
for count in range(1,5):
 for before in range(1,count+1):
  for direction,event in [(-1,0x9120409e),(1,0xb5971bf1)]:
   uc=Uc(UC_ARCH_X86,UC_MODE_32)
   for at,n in [(0x490000,0x10000),(0x4f0000,0x10000),(0x730000,0x10000),(0x2000000,0x10000)]:uc.mem_map(at,n)
   uc.mem_write(0x49cbd0,im.read(0x49cbd0,0x94));uc.mem_write(0x49c820,b'\xc2\x08\x00');uc.mem_write(0x4f8340,b'\xc3')
   uc.mem_write(0x73619c,struct.pack('<I',0x2001000));uc.mem_write(0x2001024,struct.pack('<I',count));uc.mem_write(0x200213c,struct.pack('<I',before))
   uc.mem_write(0x200eff0,struct.pack('<I',0x200f000));uc.reg_write(UC_X86_REG_ESP,0x200eff0);uc.reg_write(UC_X86_REG_ESI,0x2002000);uc.reg_write(UC_X86_REG_EBX,event)
   cues=[]
   def hook(uc,address,size,data):
    if address==0x4f8340:cues.append(uc.reg_read(UC_X86_REG_EAX))
   uc.hook_add(UC_HOOK_CODE,hook);uc.emu_start(0x49cbd0,0x200f000,count=1000)
   after=struct.unpack('<I',uc.mem_read(0x200213c,4))[0];cases.append([count,before,direction,after,len(cues),cues[0]if cues else 0])
assert len(cases)==20
lines=['// Generated from original49CBD0 bytes; UI drawing/sound callees stubbed.','// Input executable identity is checked by the generator.','struct NativeStatsSelectorCase {unsigned count,before;int direction;unsigned after,cues,notification;};','constexpr NativeStatsSelectorCase kNativeStatsSelector[]={']
lines+=['    {'+','.join(map(str,row))+'},'for row in cases];lines+=['};','']
Path('tests/post_race_selector_native.inc').write_text('\n'.join(lines),encoding='ascii')

print('PASS20 original49CBD0 executions, selected-racer clamps and conditional navigation sound')
