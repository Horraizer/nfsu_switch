"""Generate normal runtime aero pins by executing original57F2B0, no hooks."""
from pathlib import Path
import argparse,hashlib,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from career_fixed_native import FixedMachine
from hud254_trace import PE
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();assert not a.output.exists()
assert a.output.resolve()!=a.root.resolve() and a.root.resolve() not in a.output.resolve().parents
exe=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
m=FixedMachine(PE(exe));m.allowed=[(0x57f2b0,0x57f6f2),(0x575960,0x576450),(0x581b30,0x581d22),(0x46d1c0,0x46d384),(0x583ea0,0x583f20),(0x674898,0x674920),(0x4387e0,0x4387f6)]
m.put(0x78a344,0,"B");m.put(0x78a345,0,"B")
for at in (0x7361a8,0x7361a4,0x735fa0):m.put(at,0)
table=0x1110000;at=0x1100000;m.uc.mem_write(table,bun[1678864:1791424]);m.put(0x734588,table)
names=[bun[1678864+i*3216:1678864+i*3216+32].split(bytes([0]))[0].decode('ascii') for i in range(35)]
lines=['// Original57F2B0, normal non-drift; 240SX then CIVIC, sum0..9.',
 '// Reproduce with tools/race_setup_aero_native.py; retail hashes asserted there.',
 '// Each row: downforce front, rear, preload. No semantic hooks.']
for name in ['240SX','CIVIC']:
 lines.append('// '+name)
 for total in range(10):
  weight=min(total,3);susp=min(total-weight,3);tyres=total-weight-susp
  m.uc.mem_write(at,bytes(0x900));m.put(at,names.index(name));m.put(at+0x410,64);m.put(at+0x420,at+0x500)
  for i,v in enumerate([weight,susp,1,0,0,0,1,tyres,0,3,1]):m.put(at+8+i*4,v)
  m.run(0x57f2b0,(0,),edi=at,budget=3000000)
  values=[m.get(at+offset,'f') for offset in [0x2c4,0x2c8,0x2cc]]
  def literal(f):
   text=format(f,'.17g');return text+('.0' if '.' not in text and 'e' not in text else '')+'F'
  lines.append('    {'+','.join(map(literal,values))+'},')
assert not m.events
a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
assert hashlib.sha256((a.root/'speed.exe').read_bytes()).hexdigest()==hashlib.sha256(exe).hexdigest()
assert hashlib.sha256((a.root/'Global/GLOBALB.BUN').read_bytes()).hexdigest()==hashlib.sha256(bun).hexdigest()
print('PASS original57F2B0 aero20 cases, zero hooks; source files unchanged')
