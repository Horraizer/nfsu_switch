"""Bounded original road-start playlist and authored-lens witnesses.

Runs authenticated x86 selection before its RNG call and the original lens
conversion before camera submission. Supplied object storage; no hooks and no
claim of a live retail race or retail RNG history.
"""
import argparse,hashlib,json,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,BODY,CONFIG,RIDE
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true')
a=ap.parse_args();out=a.out.resolve();rel=out.relative_to(Path.cwd())
assert rel.as_posix().isascii() and rel.parts[0].startswith('build')
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);playlists=[];lenses=[]
for racers in range(1,5):
 for grid in range(1,racers+1):
  m=Machine(im);m.allowed=[(0x47bc63,0x47bd07)]
  m.put(0x7361b4,ACTOR);m.put(ACTOR+4,DRIVER);m.put(DRIVER+0x14,CONFIG);m.put(CONFIG+3,grid,'B')
  m.put(0x73619c,BODY);m.put(BODY+0x24,racers)
  for x in range(12):m.put(RIDE+4*x,1)
  m.run(0x47bc63,end=0x47bd07,eax=1,esi=RIDE)
  assert not m.events
  playlists.append([racers,grid,m.reg('ebx')])
for focal in (12.,18.,24.,35.,50.,80.):
 m=Machine(im);m.allowed=[(0x48e62f,0x48e688),(0x564b10,0x564f20),(0x401c10,0x401c50),(0x674898,0x67490d)]
 m.put(BODY+0x30,focal,'f');m.put(ACTOR+0x1c,DRIVER);m.put(0x735f5c,0)
 m.run(0x48e62f,end=0x48e688,edx=BODY,ebx=ACTOR)
 assert not m.events
 lenses.append([int(focal),m.get(DRIVER+0xc4,'H')])
report=dict(playlists=playlists,lenses=lenses,hooks=[],boundary=__doc__)
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Authenticated47BC63 playlist family and48E62F authored lens; no hooks.',
       'static constexpr unsigned kNativeStartPlaylist[][3]={']
lines+=['    {%d,%d,%d},'%tuple(r) for r in playlists]
lines+=['};','static constexpr unsigned kNativeStartLens[][2]={']
lines+=['    {%d,%d},'%tuple(r) for r in lenses]
lines+=['};'];text='\n'.join(lines)+'\n';target=Path('tests/race_start_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'race start fixture stale'
print('PASS original road startup playlist and authored lens; no hooks')
