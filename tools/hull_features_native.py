"""Capture original46EA50 feature inputs/outputs inside actual472950 queries.
The input case file contains explicit poses; retail bytes remain read-only.
"""
from pathlib import Path
import sys,json,hashlib
from world_body_trace import Image
import contact256_native as n
from contact257_retail import inputs,prepare
if len(sys.argv)!=4:raise SystemExit('usage: hull_features_native.py retail-root poses-json fresh-output')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[3]).resolve()
if out.exists() or out==root or root in out.parents:raise ValueError('fresh non-retail output required')
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for case in json.loads(Path(sys.argv[2]).read_text()):
 m=n.fixture(im,(0,0,0),(0,0,0),(0,0,0));prepare(m,n,data)
 for body,rigid,values in ((n.BODY_A,n.RIGID_A,case['input'][:12]),(n.BODY_B,n.RIGID_B,case['input'][12:])):
  n.vector(m,rigid+0x20,values[:3])
  for k in range(3):n.vector(m,rigid+0x30+16*k,values[3+3*k:6+3*k])
 trace=[]
 def snapshot(x):
  base=n.PAIR+0x60
  return dict(a=[n.vector(x,base+16*k) for k in range(x.get(base+0xc0))],
   b=[n.vector(x,base+0x40+16*k) for k in range(x.get(base+0xd4))],
   ia=[x.get(base+0xb0+4*k) for k in range(x.get(base+0xc0))],
   ib=[x.get(base+0xc4+4*k) for k in range(x.get(base+0xd4))])
 def begin(x):trace.append(dict(before=snapshot(x)))
 def end(x):
  trace[-1]['after']=snapshot(x)
  trace[-1]['wa']=n.vector(x,n.PAIR+0xe0);trace[-1]['wb']=n.vector(x,n.PAIR+0xf0)
  trace[-1]['direction']=n.vector(x,x.reg('esp')+0x10)
  trace[-1]['squared']=x.get(x.reg('esp')+8,'f')
 m.observers[0x46ea50]=begin;m.observers[0x472164]=end;m.observers[0x472314]=end
 result=n.query(m);rows.append(dict(input=case['input'],query=result,trace=trace))
out.write_text(json.dumps(rows,indent=2),encoding='ascii')
counts={}
for row in rows:
 for t in row['trace']:
  key=(len(t['before']['a']),len(t['before']['b']));counts[key]=counts.get(key,0)+1
print('PASS: actual query feature traces',len(rows),counts)
