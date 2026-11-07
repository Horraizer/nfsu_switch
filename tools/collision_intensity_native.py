"""Original contact impact/scrape intensity initialization, curves and ftol."""
import argparse,hashlib,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
b=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for kind,scale in enumerate([12,1,1,1,17,13,1]):
 for value in [-1.,0.,.1,.5,1.,1.5,2.,3.,10.,20.,39.,100.,3*scale/254,9*scale/254,1.5*scale,3*scale]:
  m=Machine(Image(b));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
  m.run(0x530340,(0,));owner,record=0x1120000,0x1130000
  m.put(owner+0x21c,record);m.put(owner+0x2f8,0x7367a4);m.put(owner+0x2fc,0x7367c0);m.put(owner+0x340,kind)
  m.put(record+0x60,value,'f');m.put(record+0x40,value,'f')
  m.run(0x530f80,ecx=owner);m.run(0x531030,ecx=owner)
  assert not m.events
  rows.append([kind,bits(value),m.get(owner+0x248),m.get(owner+0x24c)])
text='// Actual530340,530F80,531030,532840 and674898; no replaced math/IO.\nstatic constexpr unsigned kNativeCollisionIntensity[][4]={\n'+''.join(' {'+','.join('0x%08XU'%v for v in r)+'},\n' for r in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/collision_intensity_native.inc'
if a.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'native collision intensity fixture differs'
print('PASS original collision intensity curves and truncation, rows='+str(len(rows)))
