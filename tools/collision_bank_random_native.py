"""Actual impact callback64CA50 and shared651150/6511B0 state.

Foreign-bank draw counts are supplied inputs; no natural module order or
PCM/admission claim. Actor replacement does not reseed the bank generator.
There are no semantic callbacks or retail writes.
"""
from pathlib import Path
import argparse,hashlib,sys
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
raw=(a.retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[];node=0x1100000
for seed in (0,1,0xffffffff,0x80000000,0x12345678,0xdeadbeef):
 m=Machine(Image(raw));m.allowed=[(0x651150,0x651255),(0x64ca50,0x64ca74)];m.run(0x651150,(seed,))
 for tick in range(32):
  foreign=tick%4;before=[m.get(0x713fcc+4*i)for i in range(6)]
  for unused in range(foreign):m.run(0x6511b0)
  m.put(node,0xffffff6a);m.put(node+4,301);m.put(node+8,0);m.put(node+12,1)
  m.run(0x64ca50,(node,));value=m.reg('eax');jitter=value if value<0x80000000 else value-0x100000000
  rows.append([seed,tick,foreign,before,jitter,[m.get(0x713fcc+4*i)for i in range(6)]])
 assert not m.events

def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual64CA50 and651150/6511B0; explicit interleaved foreign draws.\n'
text+='struct NativeCollisionBankRandom {unsigned seed,tick,foreign,before[6];int jitter;unsigned after[6];};\n'
text+='static constexpr NativeCollisionBankRandom kNativeCollisionBankRandom[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
p=Path(__file__).resolve().parents[1]/'tests/collision_bank_random_native.inc';assert text.isascii()
if a.write_fixture:p.write_bytes(text.encode('ascii'))
else:assert p.read_text('ascii')==text,'original collision bank RNG fixture differs'
print('PASS actual collision jitter/shared bank RNG',len(rows))
