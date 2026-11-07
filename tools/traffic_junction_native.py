from pathlib import Path
import sys,hashlib,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import Machine
ap=argparse.ArgumentParser(description='Actual583BB0 traffic policy and4216F0 world RNG; supplied event/seed')
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
for kind in range(11):
 for seed in [0,1,0x3fffffff,0x40000000,0x40000001,0x7fffffff,0xffffffff]:
  m=Machine(im);m.allowed=[(0x400000,0x6a0000)]
  event,world=0x1100000,0x1104000
  m.put(event+0xc,kind,'B');m.put(0x7361f8,world);m.put(world+0x2d0,seed)
  m.run(0x583bb0,(event,),eax=0)
  assert not m.events
  rows.append([kind,seed,m.reg('eax')&255,m.get(world+0x2d0)])
fixture='// Actual583BB0 and4216F0; supplied event/seed, no hooks.\nstatic constexpr unsigned kNativeTrafficJunctions[][4]={\n'
fixture+=''.join('    {'+','.join('0x%08XU'%n for n in row)+'},\n' for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/traffic_junction_native.inc'
if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
else:assert path.read_text(encoding='ascii')==fixture,'native traffic junction fixture differs'
print('PASS actual traffic junction predicate and retained world RNG:',len(rows),'rows')
