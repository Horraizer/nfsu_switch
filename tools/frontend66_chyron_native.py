from pathlib import Path
import sys,hashlib,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
root=parser.parse_args().retail_root
b=(root/'speed.exe').read_bytes()
assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(b);im.sections=[(a,n,r) for a,n,r,_ in im.sections]
for tick,hide_started in [(20799,0),(20800,0),(20801,0),(24799,1),(24800,1),(24801,1)]:
 m=Machine(im);obj=0x1001000;m.write(obj+0x40,0);m.uc.mem_write(obj+0x44,bytes((hide_started,1)));m.write(0x73ad3c,tick);calls=[]
 def hook(uc,at,size,_):
  if at not in [0x4d7610,0x4f63a0]:return
  calls.append('%08X'%at);sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 m.uc.hook_add(UC_HOOK_CODE,hook);m.run(0x4d74c0,(0xc98356ba,0,0,0),{UC_X86_REG_ECX:obj})
 expected_calls=[] if tick in (20799,24799) else ['004D7610'] if not hide_started else ['004F63A0']
 assert calls==expected_calls
 assert m.uc.mem_read(obj+0x45,1)[0]==(0 if hide_started and tick>=24800 else 1)
 print(tick,'hide_started',hide_started,'flags',bytes(m.uc.mem_read(obj+0x44,2)).hex(),'calls',calls)
print('Native callback executed; Hide-selection and package-close calls are explicit boundary stubs, not whole retail startup')
