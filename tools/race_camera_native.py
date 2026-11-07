"""Bounded original camera cycle and primary-view projection witness.

Executes authenticated x86 with supplied player/viewport storage and original
scalar math dispatch. Stops projection before scene submission. No arithmetic
or camera callback is stubbed; this does not claim a running retail race.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,BODY,STACK
from unicorn.x86_const import UC_X86_REG_EIP
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true')
a=ap.parse_args();out=a.out.resolve();rel=out.relative_to(Path.cwd())
assert rel.as_posix().isascii() and rel.parts[0].startswith('build')
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);cycles=[]
for special in (0,1):
 for initial in (0,1,2,4):
  m=Machine(im);m.allowed=[(0x42e020,0x42e150)]
  m.put(ACTOR+4,DRIVER);m.put(DRIVER+0xc,BODY)
  m.uc.mem_write(ACTOR+0x2c,bytes((0,1,2,4)))
  m.put(BODY+0xb50,5)
  for i in range(5):m.put(BODY+0xb60+16*i,i,'H')
  m.put(0x78a345,special,'B')
  m.run(0x42e020,eax=initial,esi=ACTOR)
  assert not m.events
  cycles.append([special,initial,m.reg('eax')])
starts=[]
for special in (0,1):
 for preference in range(4):
  m=Machine(im);m.allowed=[(0x4b47a0,0x4b4a10)]
  for i,pov in enumerate((0,1,2,4)):m.put(0x758938+4*i,pov,'B')
  m.put(0x758948,preference);m.put(0x78a345,special,'B')
  m.run(0x4b47a0,eax=0)
  assert not m.events
  starts.append([special,preference,m.get(0x78a388,'B')])
rows=[]
for fov in (13020,16384):
 for width,height in ((1280,720),(640,480),(1920,1080)):
  m=Machine(im)
  # Dispatch thunk and helpers execute original code, no host math hooks.
  m.allowed=[(0x40dd00,0x40df04),(0x401cd0,0x401d55),(0x564b10,0x564c00),
             (0x674898,0x67490d)]
  view,scene,viewport,projection=ACTOR,DRIVER,BODY,0x1108000
  m.put(scene+0x40,view);m.put(scene+0x58,viewport);m.put(scene+4,1)
  m.put(view+0xbc,.5,'f');m.put(view+0xc0,10000.,'f');m.put(view+0xc4,fov,'H')
  m.put(viewport+0x14,width);m.put(viewport+0x18,height)
  m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)));m.put(0x6d4d00,0)
  m.run(0x40dd00,(scene,0,0,0),end=0x40df04,esi=projection)
  assert not m.events and m.uc.reg_read(UC_X86_REG_EIP)==0x40df04
  rows.append([fov,width,height,m.get(projection+0x40),m.get(projection+0x54)])
report={'cycles':cycles,'starts':starts,'projection':rows,'hooks':[],
        'boundary':'42E020 normal/special supplied list; 40DD00 primary viewport projection before scene submission; no running game parity claim.'}
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Authenticated original42E020 and40DD00, supplied player/viewport, no hooks.',
       'static constexpr unsigned kNativeCameraCycle[][3]={']
lines+=['    {%d,%d,%d},'%tuple(r) for r in cycles]
lines+=['};','static constexpr unsigned kNativeRaceProjection[][5]={']
lines+=['    {%d,%d,%d,0x%08xU,0x%08xU},'%tuple(r) for r in rows]
lines+=['};','static constexpr unsigned kNativeRaceCameraStart[][3]={']
lines+=['    {%d,%d,%d},'%tuple(r) for r in starts]
lines+=['};'];text='\n'.join(lines)+'\n';target=Path('tests/race_camera_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'camera fixture stale'
print('PASS original camera cycle and primary race projection; no hooks')

