"""Check only the original drift postprocessing over supplied ordinary setups."""
from pathlib import Path
import sys,hashlib,struct,json
sys.dont_write_bytecode=True
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EIP
root,out=map(Path,sys.argv[1:]);raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,b,c) for a,b,c,_ in im.sections]
rows=[]
for car in ('CIVIC','240SX','SKYLINE'):
 for level in range(4):
  original=(out/(car+'-original.bin')).read_bytes();ordinary=(out/(car+'-'+str(level)+'-ordinary.bin')).read_bytes();actual=(out/(car+'-'+str(level)+'-drift.bin')).read_bytes()
  m=Machine(im);ride,record,levels=0x1001000,0x1003000,0x1005000
  m.uc.mem_write(record,original);m.uc.mem_write(ride+0x60,ordinary[0x110:0x3c0]);m.write(levels,level);m.write(ride+0x24,level);m.write(0x78a345,1)
  m.write(m.stack+0xc,struct.unpack('<I',struct.pack('<f',level*3*.1111111119389534))[0])
  def hook(u,at,size,data):
   if at==0x57f5d9:u.reg_write(UC_X86_REG_EIP,m.stop)
  m.uc.hook_add(UC_HOOK_CODE,hook)
  m.run(0x57f337,registers={UC_X86_REG_EBX:levels,UC_X86_REG_EBP:ride+0x60,UC_X86_REG_ESI:record,UC_X86_REG_EDI:ride})
  expected=bytes(m.uc.mem_read(ride+0x60,0x2b0));diff=[0x110+k for k,(a,b) in enumerate(zip(expected,actual[0x110:0x3c0])) if a!=b]
  rows.append(dict(car=car,level=level,different_offsets=diff));print(car,level,'PASS' if not diff else 'FAIL',diff[:12])
report=out/'comparison.json';assert not report.exists();report.write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
raise SystemExit(any(r['different_offsets'] for r in rows))
