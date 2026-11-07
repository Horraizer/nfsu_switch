"""Original489855 longitudinal camera history on supplied published actor inputs."""
import argparse,hashlib,json,math,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,STACK,STOP,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();relative=out.relative_to(Path.cwd())
assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
assert a.root.resolve()!=out and a.root.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw)
f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
scenarios=[
 ('launch_coast_brake',[(12,20,0,1/60,160),(0,55,0,1/60,40),(-20,40,0,1/60,80),(0,0,0,1/60,100)]),
 ('slow_variable_clock',[(7,5,0,.4/60,60),(7,15,0,1/30,45),(-15,25,0,1/120,80),(0,0,0,0,1),(0,0,0,1/60,50)]),
 ('slip_attenuation',[(15,35,.05,1/60,70),(0,35,.05,1/60,70),(15,35,.2,1/60,30),(0,35,0,1/60,30),(-12,35,-.1,1/60,50)]),
 ('thresholds',[(1.1666666,20,0,1/60,12),(1.1666668,20,0,1/60,25),(0,20,0,1/60,20),(-6.6666665,20,0,1/60,20),(-6.666667,20,0,1/60,25)]),
 ('speed_limit',[(20,230,0,1/60,30),(20,100,0,1/60,30),(0,100,0,1/60,30),(-30,-10,0,1/60,30)])]
rows=[]
for name,segments in scenarios:
 m=Machine(im);m.allowed=[(0x489855,0x489e20),(0x674898,0x67490d),(0x564cd0,0x564ef6)]
 m.put(ACTOR+0x394,1.,'f')
 first=True
 for acc,speed,angle,dt,count in segments:
  vx=f32(speed*math.cos(angle));vy=f32(speed*math.sin(angle))
  probe=Machine(im);probe.allowed=[(0x564cd0,0x564ef6),(0x674898,0x67490d)]
  probe.run(0x564db0,(bits(vx),bits(vy)))
  heading=probe.reg('eax')&65535
  slip=f32(((0-heading)&65535)*f32(9.58738019107841e-5))
  if slip>f32(3.141590118408203):slip=f32(slip-f32(6.283180236816406))
  for tick in range(count):
   m.put(DRIVER+0x3d0,speed,'f');m.put(DRIVER+0x70,vx,'f');m.put(DRIVER+0x74,vy,'f')
   m.put(0x73457c,dt,'f');m.put(STACK+0x110,acc,'f');m.put(STACK+0x24,DRIVER)
   m.run(0x489855,end=0x489e20,ebx=ACTOR,esi=DRIVER)
   assert not m.events and not probe.events
   state=[m.get(ACTOR+offset) for offset in (0x384,0x388,0x38c,0x390)]+[m.get(STACK+0x18)]
   rows.append(dict(scenario=name,reset=first,input=list(map(bits,(acc,speed,slip,dt))),state=state));first=False
anchors=[]
for along,height,offset in ((-8.,2.,2.),(-6.5,1.8,-1.),(-8.,2.,0.)):
 m=Machine(im);entry=STOP+0x100
 # Supply ordinary forward blend=0 on the x87 stack; no native bytes patched.
 m.allowed=[(entry,entry+7),(0x489eb3,0x489f38)]
 m.uc.mem_write(entry,b'\xd9\xee\xe9'+struct.pack('<i',0x489eb3-(entry+7)))
 m.put(STACK+0x44,DRIVER);m.put(STACK+0x18,offset,'f')
 m.put(DRIVER,0,'H');m.put(DRIVER+4,along,'f');m.put(DRIVER+8,height,'f')
 m.run(entry,end=0x489f38)
 assert not m.events
 anchors.append(dict(input=list(map(bits,(along,height,offset))),result=[bits(-m.get(STACK+0x20,'f')),m.get(STACK+0x14)]))
report=dict(rows=rows,anchors=anchors,hooks=[],boundary='Original489855..489E20, ordinary mode32C=0. Supplied local acceleration, published velocity/heading and73457C dt. No full mover/terrain/GPU equivalence claim.')
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Original489855..489E20 with original564DB0 slip producer; no hooks.',
 'struct NativeCameraSurge {bool reset;std::array<unsigned,4> input;std::array<unsigned,5> state;};',
 'static constexpr NativeCameraSurge kNativeCameraSurge[]={']
array=lambda v:'{{'+','.join('0x%08xU'%x for x in v)+'}}'
for row in rows:lines.append('    {%s,%s,%s},'%('true' if row['reset'] else 'false',array(row['input']),array(row['state'])))
lines.extend(['};','struct NativeCameraSurgeAnchor {std::array<unsigned,3> input;std::array<unsigned,2> result;};',
 'static constexpr NativeCameraSurgeAnchor kNativeCameraSurgeAnchor[]={'])
for row in anchors:lines.append('    {%s,%s},'%(array(row['input']),array(row['result'])))
lines.append('};');text='\n'.join(lines)+'\n';target=Path('tests/camera_surge_native.inc')
assert text.isascii()
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'camera surge fixture stale'
print('PASS original camera surge sequence; supplied actor boundary, no hooks')
