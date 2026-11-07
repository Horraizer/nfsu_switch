"""Authenticated472F30 support walk, selected retail CV and explicit directions.
External allocator/QPC fixtures only; no original code patched.
"""
from pathlib import Path
import sys,json,hashlib
from world_body_trace import Image
import contact256_native as n
from contact257_retail import inputs,prepare
if len(sys.argv)!=3:raise SystemExit('usage: hull_support_native.py retail-root fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out.exists() or out==root or root in out.parents:raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for direction in ((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1),(1,.2,.7),(0,0,0)):
 for start in (0,11):
  m=n.fixture(im,(4,0,0),(0,0,0),(0,0,0));prepare(m,n,data)
  d,vertex,axes,result=0x1140000,0x1140100,0x1140200,0x1140300
  n.vector(m,d,direction);m.put(vertex+12,start,'h');n.matrix(m,axes)
  m.run(0x472f30,(0x29f0000,d,vertex,0,axes,axes,result),budget=3000000)
  rows.append(dict(direction=direction,start=start,index=m.get(result+12,'h'),
   point=n.vector(m,result+16),walk=m.get(0x735fd4)))
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(rows,indent=2),encoding='ascii')
def f(x):return float(x).hex()+'F'
lines=['// Original472F30: retail240SX_CV, COM(.25,0,.28), identity axes.']
for row in rows:
 lines.append('{ContactVector d{{'+','.join(f(v) for v in row['direction'])+'}};')
 lines.append('unsigned index=999;ContactVector point{};')
 lines.append('check(race_hull_support(first,spec,box,d,'+str(row['start'])+',index,point),"native support executes");')
 lines.append('check(index=='+str(row['index'])+' && point==ContactVector{{'+','.join(f(v) for v in row['point'])+'}},"native support index and world point");}')
(out/'pins.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
print('PASS:16 authenticated support walk cases; global walk='+str(rows[0]['walk']))
