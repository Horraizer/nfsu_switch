"""Original45E970 ordinary/drag steering gates on supplied actor distance.

Executes the complete driver on declared poses/paths, without hooks.
This is not a whole-world construction or a race trajectory.
"""
import argparse,hashlib,itertools,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from controller293_native import fixture
from physics256_native import ACTOR,DRIVER,PHYSICS,Machine,bits
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('retail',type=Path);p.add_argument('--write-fixture',action='store_true')
a=p.parse_args()
raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
for distance,side in itertools.product((0,bits(2.)-1,bits(2.),bits(2.)+1,bits(10.)),(-1,1)):
 m=fixture(image,[(10.,side*10.,0.),(100.,side*60.,0.)],speed=12.,target=12.,category=3)
 m.put(ACTOR+0x28,1);m.put(ACTOR+0x24,2);m.put(ACTOR+0x1c,0x1154000)
 m.put(ACTOR+0x3c,0x1153000);m.put(PHYSICS+0x48,0x1155000)
 m.put(0x78a344,1,'B');m.put(ACTOR+0x3b8,distance)
 m.run(0x45e970,(bits(1/60),),ecx=DRIVER)
 assert not m.events,m.events
 rows.append((distance,side,m.get(DRIVER+0x1d0,'H')))
text='// Original45E970, selector1/controller2, distance gate, supplied path.\n'
text+='static constexpr struct {unsigned distance;int side;unsigned word;} kNativeDragSteering[]={\n'
text+=''.join('{%dU,%d,%dU},\n'%r for r in rows)+'};\n'
ordinary=[]
for distance,side in itertools.product((0,bits(2.)-1,bits(2.),bits(2.)+1,bits(10.)),(-1,1)):
 m=fixture(image,[(10.,side*10.,0.),(100.,side*60.,0.)],speed=12.,target=12.,category=3)
 # The fixture's original selector3/controller3 takes the ordinary branch.
 m.put(ACTOR+0x3b8,distance)
 m.run(0x45e970,(bits(1/60),),ecx=DRIVER)
 assert not m.events,m.events
 ordinary.append((distance,side,m.get(DRIVER+0x1d0,'H')))
text+='// Original45E970, selector3/controller3, same actor-distance boundary.\n'
text+='static constexpr struct {unsigned distance;int side;unsigned word;} kNativeOrdinarySteering[]={\n'
text+=''.join('{%dU,%d,%dU},\n'%r for r in ordinary)+'};\n'
events=[]
for distance,speed,event in itertools.product((0,bits(2.)-1,bits(2.),bits(2.)+1,bits(10.)),(0.,100.),(0xa6,0xa7)):
 m=Machine(image);m.allowed=[(0x4605e0,0x460784)]
 m.put(0x73608c,0);m.put(0x7003d8,1)
 controller=0x1140000
 m.put(DRIVER+0x1f0,ACTOR);m.put(ACTOR+0x2c,controller)
 m.put(ACTOR+0x3b8,distance);m.put(ACTOR+0x3d4,speed,'f')
 # A release exercises the complete event path without geometry callbacks.
 m.put(controller+0x500,0)
 m.run(0x4605e0,(0,event,0,DRIVER))
 assert not m.events,m.events
 events.append((distance,bits(speed),event,m.get(controller+0x500)))
text+='static constexpr struct {unsigned distance,speed,event,mode;} kNativeDragInput[]={\n'
text+=''.join('{%dU,%dU,%dU,%dU},\n'%r for r in events)+'};\n'
target=Path(__file__).resolve().parents[1]/'tests/drag_steering_native.inc'
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text('ascii')==text,'original drag distance gate changed'
print('PASS original ordinary/drag driver and drag input distance gates, cases='+str(len(rows)+len(ordinary)+len(events)))
