"""Actual651150/6511B0 seed and consecutive carry-state fixtures, no hooks."""
import argparse,hashlib,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');args=p.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
seed_rows=[];rows=[]
initials=[]
for seed in [0,1,0xffffffff,0x80000000,0x12345678,0xdeadbeef]:
 m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)];m.run(0x651150,(seed,));state=[m.get(0x713fcc+4*i)for i in range(6)]
 seed_rows.append([seed]+state);initials.append(state)
 assert not m.events
initials.extend([[0]*6,[0xffffffff]*6,[0x80000000,0x11111111,0xdddddddd,0xaaaaaaaa,0xfffffffe,0xffffffff],[0,0,0,0,0,0xffffffff]])
for scenario,initial in enumerate(initials):
 m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)]
 for i,v in enumerate(initial):m.put(0x713fcc+4*i,v)
 for tick in range(64):
  before=[m.get(0x713fcc+4*i)for i in range(6)];m.run(0x6511b0)
  rows.append([scenario,tick]+before+[m.reg('eax')]+[m.get(0x713fcc+4*i)for i in range(6)])
 assert not m.events
text='// Actual651150 and6511B0, synthetic seed/carry inputs; no substituted calls.\n'
for name,data in [('kNativeBankAudioSeed',seed_rows),('kNativeBankAudioRandom',rows)]:
 text+='static constexpr unsigned '+name+'[]['+str(len(data[0]))+']={\n'+''.join(' {'+','.join('0x%08XU'%v for v in r)+'},\n'for r in data)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/bank_audio_random_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original bank RNG fixture differs'
print('PASS original bank RNG constructor and consecutive carry state; rows='+str(len(rows)))
