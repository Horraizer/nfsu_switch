"""Actual57A530 ordinary-human world path on explicit source-state inputs.

Type6 and owner1 use original456D50/57A820/59C1A0, body getters, angle and
456DE0. A separate synthetic body/controller slice supplies published pose,
velocity, magnitude and forward axes; no factory or physical trajectory is
claimed. The getter binding is independently observed on initialized original
HumanController. No semantic callback or retail byte patch is used.
"""
from pathlib import Path
import argparse,hashlib,itertools,json,sys
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();raw=(options.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);node,actor,controller,vtable,proxy,body=0x1100000,0x1110000,0x1120000,0x1121000,0x1130000,0x1140000
rows=[]
for origin,velocity,forward,magnitude in itertools.product(((10.,20.,3.),(10000.25,-10000.5,-2.)),((0.,0.,0.),(.5,0.,0.),(1.,0.,0.),(12.,3.,2.),(-20.,4.,0.),(0.,0.,20.),(2.3,5.1,1.1)),((1.,0.,0.),(0.,1.,0.),(-1.,0.,0.),(0.,-1.,0.),(.6,.8,0.)),(0.,10.,40.)):
 m=Machine(image);m.allowed=[(0x57a530,0x57a5ec),(0x456d50,0x456de0),(0x57a820,0x57a8e0),(0x647b70,0x647b81),(0x59c1a0,0x59c1a7),(0x578730,0x578830),(0x564bd0,0x564f10),(0x456de0,0x456e8a),(0x458f10,0x459096),(0x4594d0,0x4596b0),(0x567370,0x5673e0),(0x674898,0x67490d)]
 m.put(node+0x1d4,proxy);m.put(proxy+0x24,6);m.put(proxy+0x90,actor);m.put(proxy+0x1c,body)
 m.put(actor+0x24,1);m.put(actor+0x2c,controller);m.put(actor+0x3d4,magnitude,'f')
 m.put(controller,vtable);m.put(controller+4,actor);m.put(vtable+0x50,0x59c1a0)
 for offset,values in((0x10,forward),(0x20,(-forward[1],forward[0],0.)),(0x30,(0.,0.,1.)),(0x40,origin),(0x90,velocity)):
  for k,value in enumerate(values):m.put(body+offset+4*k,value,'f')
 m.put(body+0x4c,1.,'f');m.put(body+0xe4,0)
 m.run(0x57a530,ebx=node,budget=30000);assert not m.events
 path=node+0x10;count=m.get(path,'B');assert count==1
 rows.append([[bits(v)for v in origin],[bits(v)for v in velocity],[bits(v)for v in forward],bits(magnitude),[m.get(path+0x10+4*k)for k in range(3)],m.get(path+0x1ac),m.get(path+0x150,'B'),m.get(path+0x164,'H'),m.get(path+0x18c,'B')])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual57A530 owner1 / type6 world publication with original source getters.\n'
text+='struct NativeHumanWorldPath {unsigned origin[3],velocity[3],forward[3],magnitude,point[3],length,route,index,category;};\n'
text+='static const NativeHumanWorldPath kNativeHumanWorldPath[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/human_world_path_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original human world path fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,rows=rows),indent=2)+'\n',encoding='ascii')
print('PASS original ordinary-human world path',len(rows))
