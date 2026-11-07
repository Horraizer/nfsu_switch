"""Original view motion and fresh-profile crash option; no renderer or file IO."""
import argparse,hashlib,json,math,random,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,ACTOR,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();relative=out.relative_to(Path.cwd())
assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
assert a.root.resolve()!=out and a.root.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);m=Machine(im);m.allowed=[(0x4aa271,0x4aa27f),(0x4b47df,0x4b4803)]
m.run(0x4aa271,end=0x4aa27f,esi=0x7588c4)
default=m.get(0x758951,'B');m.run(0x4b47df,end=0x4b4803)
assert m.get(0x78a3fa,'B')==default and not m.events
rng=random.Random(2701);rows=[]
for dt in (0.,1./60.,1./30.,.4/60.,.1):
 for cut in (0,1):
  for j in range(8):
   current=[rng.uniform(-2500,2500),rng.uniform(-2500,2500),rng.uniform(-10,60)]
   previous=[v+rng.uniform(-2,2) for v in current];old=[1.25,-2.5,3.75]
   m=Machine(im);m.allowed=[(0x47d1db,0x47d291)]
   for k in range(3):
    m.put(ACTOR+0x40+4*k,current[k],'f');m.put(ACTOR+0x110+4*k,previous[k],'f');m.put(ACTOR+0x1e0+4*k,old[k],'f')
   m.put(ACTOR+0x270,cut,'B');m.put(ACTOR+0x274,dt,'f')
   m.run(0x47d1db,end=0x47d291 if dt>0 or cut else 0x47d624,ebx=ACTOR)
   assert not m.events and m.get(ACTOR+0x270,'B')==0
   rows.append(dict(current=list(map(bits,current)),previous=list(map(bits,previous)),old=list(map(bits,old)),dt=bits(dt),cut=cut,velocity=[m.get(ACTOR+0x1e0+4*k) for k in range(3)]))
view_rows=[]
for j in range(32):
 yaw=rng.uniform(-math.pi,math.pi);pitch=rng.uniform(-.4,.4)
 f=[math.cos(pitch)*math.cos(yaw),math.cos(pitch)*math.sin(yaw),-math.sin(pitch)]
 up=[math.sin(pitch)*math.cos(yaw),math.sin(pitch)*math.sin(yaw),math.cos(pitch)]
 right=[f[1]*up[2]-f[2]*up[1],f[2]*up[0]-f[0]*up[2],f[0]*up[1]-f[1]*up[0]]
 velocity=[rng.uniform(-80,80) for k in range(3)]
 m=Machine(im);m.allowed=[(0x5ac449,0x5ac4e8)]
 matrix,source,target=ACTOR,ACTOR+0x100,ACTOR+0x200
 for k in range(3):
  m.put(source+k*4,velocity[k],'f')
  for column,axis in enumerate((right,[-x for x in up],f)):m.put(matrix+4*(4*k+column),axis[k],'f')
 m.run(0x5ac449,(target,source,matrix));assert not m.events
 view_rows.append(dict(velocity=list(map(bits,velocity)),right=list(map(bits,right)),up=list(map(bits,up)),forward=list(map(bits,f)),result=[m.get(target+4*k) for k in range(3)]))
report=dict(default_crash_intensity=default,motion=rows,view=view_rows,hooks=[],boundary='Original4AA271/4B47DF profile publication and47D1DB view velocity. Supplied post-inversion world eyes and unscaled dt; no camera mover or GPU equivalence claim.')
(out/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Original4AA271/4B47DF and47D1DB. Supplied published eyes/dt; no hooks.',
 'static constexpr unsigned kNativeCrashDefault=%dU;'%default,
 'struct NativeCameraMotion {std::array<unsigned,3> eye,previous,old;unsigned dt;bool cut;std::array<unsigned,3> velocity;};',
 'static constexpr NativeCameraMotion kNativeCameraMotion[]={']
array=lambda v:'{{'+','.join('0x%08xU'%x for x in v)+'}}'
for r in rows:lines.append('    {%s,%s,%s,0x%08xU,%s,%s},'%(array(r['current']),array(r['previous']),array(r['old']),r['dt'],'true' if r['cut'] else 'false',array(r['velocity'])))
lines.append('};')
lines.extend(['// Original scalar D3DX normal transform. Native view Y is negative portable up.',
 'struct NativeCameraVelocity {std::array<unsigned,3> velocity,right,up,forward,result;};',
 'static constexpr NativeCameraVelocity kNativeCameraVelocity[]={'])
for r in view_rows:lines.append('    {%s,%s,%s,%s,%s},'%(array(r['velocity']),array(r['right']),array(r['up']),array(r['forward']),array(r['result'])))
lines.append('};');text='\n'.join(lines)+'\n';target=Path('tests/camera_presentation_native.inc')
assert text.isascii()
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'camera presentation fixture stale'
print('PASS original view motion and profile publication; supplied matrix-eye boundary, no hooks')
