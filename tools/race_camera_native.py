"""Bounded original camera cycle and primary-view projection witness.

Executes authenticated x86 with supplied player/viewport storage and original
scalar math dispatch. The widescreen pass applies the primary-view operands
from the PC fix with Scaling=0, then executes the same native projection.
Both passes stop before scene submission. A separate scalar test executes
the type-17 eye channel during a stationary view change. A second test
runs the race mover under a bounded acceleration step on flat ground.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,BODY
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
rows=[]; widescreen=[]
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
  # Primary view (1), Widescreen Fix with Scaling=0. Patch only the
  # aspect input and three scalar operands in emulator memory.
  patch=0x1109000
  m.put(patch,1.,'f')
  m.put(patch+4,1./(float(width)/height/(4./3.)),'f')
  m.put(patch+8,.4525,'f')
  m.put(patch+12,1.28,'f')
  m.uc.mem_write(0x40ddd2,b'\xd9\x05'+struct.pack('<I',patch)+b'\xc7\x44\x24\x20\x00\x00\x80\x3f')
  m.put(0x40de38,patch+8)
  m.put(0x40debd,patch+4)
  m.put(0x40deee,patch+12)
  m.uc.ctl_remove_cache(0x40dd00,0x40df04)
  m.run(0x40dd00,(scene,0,0,0),end=0x40df04,esi=projection)
  assert not m.events and m.uc.reg_read(UC_X86_REG_EIP)==0x40df04
  widescreen.append([fov,width,height,m.get(projection+0x40),m.get(projection+0x54)])
# Flat, motionless 240SX, switching far -> near. Both aim and direction
# remain constant; the eye follows the original type-17 scalar channel.
m=Machine(im);m.allowed=[(0x593d40,0x593e59)]
channel=ACTOR
m.put(channel,-5.8,'f');m.put(channel+8,-5.8,'f')
m.put(channel+0x24,1.,'f');m.put(channel+0x28,1.,'f')
m.put(channel+0x2e,1,'H')
dt=struct.unpack('<I',struct.pack('<f',1/60))[0]
eye=[]
for frame in range(121):
 m.run(0x593d40,(dt,0,0),ecx=channel)
 m.put(channel+8,-4.7,'f')
 if m.get(channel,'f')!=m.get(channel+8,'f'):
  m.put(channel+0x2c,2,'H')
 if frame in (0,1,2,10,30,60,120):
  eye.append([frame,m.get(channel)])
assert not m.events


def acceleration_step(im):
    m=Machine(im)
    m.allowed=[(0x400000,0x680000)]
    m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)))
    m.put(0x6d4d00,0)
    m.hook(0x440a40,'flat-ground material',callback=lambda m:(m.put(m.get(m.reg('esp')+12),1),m.ret(20)))
    m.scalar_hook(0x441d80,'zero road height noise')
    m.hook(0x483640,'continuous shake excluded',pop=16)
    m.hook(0x483b90,'geometry clearance excluded',pop=12)
    m.hook(0x575040,'impacts excluded')
    m.hook(0x41ffe0,'human actor',value=ACTOR)
    m.hook(0x43a2e0,'running race',value=1)
    m.put(0x6fbf14,0)
    m.put(0x7361f0,0)
    m.put(0x73457c,1/60,'f')
    m.put(0x73ad38,1/60,'f')
    m.put(0x73ad34,0)
    mover,view,cfg=DRIVER,BODY,0x1105000
    m.put(mover,0x6c7e58);m.put(mover+0x1c,view);m.put(mover+0x2c,ACTOR)
    m.put(mover+0x40,1);m.put(mover+0x44,1,'B')
    m.put(ACTOR+0xc,cfg)
    def vec(addr, xyz):
        m.uc.mem_write(addr, struct.pack('<4f', *xyz, 0))

    for off, axis in ((0x90,(1,0,0)), (0xa0,(0,1,0)), (0xb0,(0,0,1))):
        vec(ACTOR + off, axis)
    m.put(ACTOR + 0x3cc, 4)
    m.put(cfg + 0xb50, 5)
    anchors = ((-5.8,1.7,0,0), (-4.7,1.7,0,0), (.716,.79,500,0),
               (-.195,.805,0,-.44), (-5.8,3.45,3040,0))
    for i, (along, height, pitch, across) in enumerate(anchors):
        row = struct.pack('<HHfff', i, pitch, along, height, across)
        m.uc.mem_write(cfg + 0xb60 + 16*i, row)
    starts = ((0x30,(13020,)), (0x34,(-4.7,0,1.7)),
              (0x38,(0,0,1.7)), (0x3c,(1,0,0)))
    for i, (off, values) in enumerate(starts):
        channels = 0x1110000 + 0x100*i
        m.put(mover + off, channels)
        for j, value in enumerate(values):
            channel = channels + j*0x30
            m.put(channel, value, 'f')
            m.put(channel + 8, value, 'f')
            m.put(channel + 0x28, 1., 'f')
            m.put(channel + 0x2e, 1, 'H')
    ring = 0x1110400
    m.put(mover + 0x50, ring)
    m.put(ring + 5, 4, 'B')
    m.put(ring + 8, 0x1111000)
    samples = []
    for frame in range(180):
        time = frame / 60
        m.put(0x73ad34, int(time * 4000))
        vec(ACTOR + 0x50, (time * 20, 0, 0))
        vec(ACTOR + 0x60, (time * 20 + .2, 0, .26))
        vec(ACTOR + 0x70, (20, 0, 0))
        vec(ACTOR + 0x80, (4 if frame >= 20 else 0, 0, 0))
        m.put(ACTOR + 0x3d4, 20., 'f')
        m.run(0x48bcb0, ecx=mover)
        if frame in (0, 20, 21, 24, 40, 60, 120, 179):
            samples.append([frame, m.get(view + 0x40)])
    hooks = sorted({row['hook'] for row in m.events})
    assert set(hooks) == {
        'flat-ground material', 'zero road height noise', 'continuous shake excluded',
        'geometry clearance excluded', 'impacts excluded', 'human actor', 'running race',
    }
    return samples, hooks


def wall_clearance(im):
    m = Machine(im)
    m.allowed = [(0x400000, 0x680000)]
    m.uc.mem_write(0x6d4ac0, bytes(m.uc.mem_read(0x6d4be0, 0x47 * 4)))
    m.put(0x6d4d00, 0)
    polygon, context = 0x1110000, 0x1111000
    origin, eye, result = 0x1112000, 0x1112010, 0x1112020

    def vec(addr, xyz):
        m.uc.mem_write(addr, struct.pack('<4f', *xyz, 0))

    m.put(DRIVER, 0x6c7e58)
    m.put(context + 0x20, polygon)
    m.put(context + 0x2a0, 1)
    m.put(polygon + 9, 4, 'B')
    m.put(polygon + 0xb, 1, 'B')
    vec(polygon + 0x70, (1, 0, 0))
    corners = ((-3,-3,-2), (-3,3,-2), (-3,3,4), (-3,-3,4))
    for i, corner in enumerate(corners):
        vec(polygon + 0x30 + 16 * i, corner)
    vec(origin, (.2, 0, .26))

    samples = []
    for desired in ((-4.7, 0, 1.7), (-2.9, 0, 1.7), (-2.9, 6, 1.7)):
        vec(eye, desired)
        m.run(0x4838e0, (DRIVER, context, origin, eye, result))
        correction = struct.unpack('<3f', m.uc.mem_read(result, 12))
        samples.append([desired, [p + d for p, d in zip(desired, correction)]])
    assert not m.events
    assert m.get(0x6b6eb8 + 0x40) == 0

    # Supply the polygon that the synthetic camera would collect from the
    # collision tree. The native mover still clips and rebuilds its matrix.
    def candidates(machine):
        collection = machine.reg('edi')
        machine.put(collection + 0x2a0, 1)
        machine.put(collection + 0x20, polygon)
        machine.put(collection + 0xb0, polygon)
        machine.put(polygon + 8, 1, 'B')
        machine.ret(12)

    m.uc.mem_map(0, 0x1000)
    m.hook(0x4407b0, 'synthetic wall candidate', callback=candidates)
    m.put(ACTOR + 0x3c, BODY)
    vec(ACTOR + 0x60, (.2, 0, .26))
    m.put(BODY + 0xf0, -.45, 'f')
    m.put(BODY + 0x100, .65, 'f')
    matrix = 0x1112100
    view = (0,0,1,0, -1,0,0,0, 0,-1,0,0, 0,1.7,4.7,1)
    m.uc.mem_write(matrix, struct.pack('<16f', *view))
    m.run(0x483b90, (DRIVER, matrix, ACTOR), ecx=0)
    assert [row['hook'] for row in m.events] == ['synthetic wall candidate']
    corrected = struct.unpack('<16f', m.uc.mem_read(matrix, 64))
    return samples, corrected[2:11:4]


acceleration, acceleration_hooks = acceleration_step(im)
wall_points, wall_forward = wall_clearance(im)
report={'eye_response':eye,'acceleration_step':acceleration,'acceleration_hooks':acceleration_hooks,
        'wall_clearance':wall_points, 'wall_forward':wall_forward, 'ordinary_continuous_shake_gain':0,'cycles':cycles,'starts':starts,'projection':rows,'widescreen':widescreen,'hooks':[],
        'boundary':'42E020 normal/special list; 40DD00 primary viewport projection with and without widescreen operands; no running game parity claim.'}
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Authenticated original42E020 and40DD00, supplied player/viewport, no hooks.',
       'static constexpr unsigned kNativeCameraCycle[][3]={']
lines+=['    {%d,%d,%d},'%tuple(r) for r in cycles]
lines+=['};','static constexpr unsigned kNativeRaceProjection[][5]={']
lines+=['    {%d,%d,%d,0x%08xU,0x%08xU},'%tuple(r) for r in rows]
lines+=['};','// 40DD00 with the PC widescreen primary-view operands (Scaling=0).',
        'static constexpr unsigned kNativeRaceWidescreenProjection[][5]={']
lines+=['    {%d,%d,%d,0x%08xU,0x%08xU},'%tuple(r) for r in widescreen]
lines+=['};','static constexpr unsigned kNativeRaceCameraStart[][3]={']
lines+=['    {%d,%d,%d},'%tuple(r) for r in starts]
lines+=['};','// Native 593D40 eye response, stationary far-to-near transition.',
        'static constexpr unsigned kNativeRaceEyeResponse[][2]={']
lines+=['    {%d,0x%08xU},'%tuple(r) for r in eye]
lines+=['};','// Native 48BCB0 world eye, straight motion with acceleration step.',
        'static constexpr unsigned kNativeRaceAcceleration[][2]={']
lines+=['    {%d,0x%08xU},'%tuple(r) for r in acceleration]
lines+=['};'];text='\n'.join(lines)+'\n';target=Path('tests/race_camera_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'camera fixture stale'
print('PASS original camera cycle, projection, type-17 POV transition, acceleration step and wall clearance')

