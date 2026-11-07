"""Actual45A2F0/456DE0 on explicit finite motion input vectors.

Only the authenticated executable is read. Origin, velocity, acceleration,
heading and requested length are inputs. Full output geometry/metadata are
pinned. No semantic hooks or retail writes. World4576B0 branch ownership,
57A820's live length producer and natural traffic trajectories are excluded.
"""
from pathlib import Path
import sys,hashlib,json,itertools,struct,argparse,math
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();raw=(options.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[];linear=[]
position,vector,acceleration,path=0x1100000,0x1101000,0x1102000,0x1103000
ranges=[(0x45a2f0,0x45a4c2),(0x456de0,0x456e8a),(0x458f10,0x459096),(0x4594d0,0x4596b0),(0x564bd0,0x564cd0),(0x567370,0x5673e0)]
def number(word):return struct.unpack('<f',struct.pack('<I',word))[0]
def f(value):return number(bits(value))
def machine(origin,velocity,accel):
 m=Machine(image);m.allowed=ranges
 for address,values in((position,origin),(vector,velocity),(acceleration,accel)):
  for k,value in enumerate(values):m.put(address+4*k,value,'f')
 return m
def snapshot(m):
 count=m.get(path,'B');assert count<=20
 return [count,[[m.get(path+0x10+j*16+4*k)for k in range(3)]for j in range(count)],m.get(path+0x1ac),[m.get(path+0x150+j,'B')for j in range(count)],[m.get(path+0x164+j*2,'H')for j in range(count)],[m.get(path+0x18c+j,'B')for j in range(count)]]
velocities=((0.,0.,0.),(.5,0.,0.),(1.,0.,0.),(9.9999,0.,0.),(10.,0.,0.),(12.,3.,2.),(-20.,4.,0.),(0.,0.,20.),(2.3,5.1,1.1))
accelerations=((0.,0.,0.),(1.,2.,0.),(-5.,0.,0.),(-8.,0.,0.),(9.,0.,0.),(number(bits(9.)+1),0.,0.),(100.,0.,0.))
for origin,velocity,accel,length,heading in itertools.product(((10.,20.,3.),(10000.25,-10000.5,-2.)),velocities,accelerations,(-1.,0.,1.,50.,80.),(12000,45000)):
 m=machine(origin,velocity,accel)
 m.run(0x45a2f0,(bits(length),vector,acceleration,heading),eax=position,ecx=path,budget=30000)
 rows.append([[bits(v)for v in origin],[bits(v)for v in velocity],[bits(v)for v in accel],heading,bits(length),*snapshot(m)])
# The old straight adapter's length is explicitly supplied, not certified as a
# complete57A820 owner. This isolates456DE0 normalization and heading output.
for velocity,heading in itertools.product(velocities,(0,16000,45000)):
 v=list(map(f,velocity));speed=f(math.sqrt(f(f(f(v[0]*v[0])+f(v[1]*v[1]))+f(v[2]*v[2]))));length=max(50.,f(speed*4.))
 m=machine((10.,20.,3.),velocity,(0.,0.,0.));m.run(0x456de0,(bits(length),heading),eax=position,edx=vector,ecx=path,budget=30000)
 linear.append([[bits(v)for v in velocity],heading,bits(length),*snapshot(m)])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual45A2F0/456DE0 with supplied arguments, no semantic hooks.\n'
text+='struct NativeMotionPrediction {unsigned origin[3],velocity[3],acceleration[3],heading,limit,count,points[20][3],length,route[20],point[20],category[20];};\n'
text+='static const NativeMotionPrediction kNativeMotionPrediction[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
text+='struct NativeLinearPrediction {unsigned velocity[3],heading,limit,count,points[20][3],length,route[20],point[20],category[20];};\n'
text+='static const NativeLinearPrediction kNativeLinearPrediction[]={\n'+''.join(lit(row)+',\n'for row in linear)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/motion_prediction_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'actual motion prediction fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,rows=rows,linear=linear),indent=2)+'\n',encoding='ascii')
print('PASS actual sampled/fallback motion output',len(rows),'straight primitive',len(linear))
