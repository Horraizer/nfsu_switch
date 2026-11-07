"""Original462390/458B90 racer horizons on supplied actor speed/globals.

The original executable remains unchanged. A declared x87 result-store stub
only reads returned ST0; no native algorithm or semantic result is hooked.
This does not establish complete controller scheduling or a natural race.
"""
from pathlib import Path
import sys,argparse,hashlib,struct
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true')
a=ap.parse_args();raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
for maneuver,prediction in ((2.,3.),(0.,0.),(4.,3.),(2.,4.)):
 for speed in (0.,10.,20.,35.,55.,80.):
  m=Machine(image);m.allowed=[(0x462390,0x4623d0),(0x458b90,0x458bc7),(0x1104000,0x1104007)]
  controller,actor,vtable,output=0x1100000,0x1101000,0x1102000,0x1103000
  m.put(controller,vtable);m.put(vtable+0x50,0x462390);m.put(controller+4,actor)
  m.put(0x7006ec,maneuver,'f');m.put(0x7006f0,prediction,'f')
  m.uc.mem_write(0x1104000,b'\xd9\x1d'+struct.pack('<I',output)+b'\xc3')
  result=[]
  for value in (20.,speed):
   m.put(actor+0x3d4,value,'f');m.run(0x458b90,ecx=controller);m.run(0x1104000)
   result.append(m.get(output))
  assert not m.events
  rows.append([bits(speed),bits(maneuver),bits(prediction),*result])
text='// Original462390/458B90, supplied speed and current global values.\n'
text+='static constexpr unsigned kNativeRoadHorizons[][5]={\n'
text+=''.join('{'+','.join(str(v)+'U'for v in row)+'},\n'for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/ai_context_horizon_native.inc'
if a.write_fixture:path.write_bytes(text.encode('ascii'))
else:assert path.read_bytes()==text.encode('ascii'),'native road horizon fixture differs'
print('PASS original racer context horizons, '+str(len(rows))+' cases, no semantic hooks')
