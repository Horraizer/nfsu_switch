"""Original controlled car/car episodes; common kind0 response, selector3.

Reuses authenticated constructors/query/job/response from contact257_episode.
Explicit synthetic poses, zero duration and stock240SX physical inputs.
Full optional effects and live event0 scheduling remain outside this fixture.
"""
from pathlib import Path
import sys,json,hashlib
from world_body_trace import Image,jdlz
import contact256_native as n
from contact257_retail import inputs
from contact257_episode import episode,TRACK_SHA
if len(sys.argv)!=3:raise SystemExit('usage: contact_flow_native.py retail-root output-json')
output=Path(sys.argv[2]).resolve();root=Path(sys.argv[1]).resolve()
if output==root or root in output.parents:raise ValueError('retail output forbidden')
if output.exists():raise ValueError('output exists; select a fresh evidence path')
root=Path(sys.argv[1]);raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
track=(root/'Tracks/TRACKB1003.lzc').read_bytes();assert hashlib.sha256(track).hexdigest().upper()==TRACK_SHA
im=Image(raw);data=inputs(root);decoded=jdlz(track);rows=[]
for offset in ((3.,0.,0.),(3.,.7,0.),(3.,.7,.2)):
 row=episode(im,n,data,decoded,offset,(10.,0.,0.),(-10.,0.,0.));row['input_offset']=offset
 # Preserve only fields needed to reproduce the numeric comparison here.
 rows.append({k:v for k,v in row.items() if k not in ('executed','phases')});print('PASS original queued pair',offset)
output.write_text(json.dumps(rows,indent=2),encoding='ascii')
