"""Actual44F1B0, real shape getter and original scalar local-frame math.
Supplied raw boxes/poses and fresh world coordinate records are explicit
inputs. This does not replay natural registration/cache publication order.
No function/result hooks, replacement trig or host matrix helper is used.
"""
from pathlib import Path
import sys,json,hashlib,argparse
sys.dont_write_bytecode=True;root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine,bits
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('retail_root',type=Path);parser.add_argument('--write-fixture',action='store_true');args=parser.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)];m.run(0x5b065b,(0,))
self,body,entry,obj,other,coord,shape,bounds,out=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000,0x1106000,0x1107000,0x1108000
m.put(self+0x3c,body);m.put(entry+0x1d4,obj);m.put(obj,0x6ba5a8);m.put(obj+0x90,other);m.put(obj+0x1c,coord);m.put(other+0x3c,shape);m.put(shape+0x10,bounds);m.put(bounds+0x10,3)
for a,values in [(body+0xf0,[-2.,-1.,-1.]),(body+0x100,[2.,1.,1.]),(bounds+0x20,[0.,0.,0.]),(bounds+0x30,[2.,1.,1.])]:
 for j,v in enumerate(values):m.put(a+j*4,v,'f')
for j,v in enumerate([1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]):m.put(coord+0x10+j*4,v,'f')

rows=[]
# Local shape3 bounds execute472AA0. Other pose/velocity are supplied fresh
# coordinate records (E4=0), not an inferred natural-world update sequence.
profiles=[([-2.,-1.,-1.],[2.,1.,1.],[-2.5,-.9,-1.],[2.5,.9,1.]),([-1.8,-.8,-.5],[2.2,1.1,1.],[-3.,-1.3,-1.],[3.,1.3,1.])]
motions=[([20.,0.,0.],20.,20.),([0.,20.,0.],20.,20.),([12.,16.,0.],20.,20.),([-12.,16.,0.],-20.,20.),([.5,0.,0.],.5,.5),([0.,0.,0.],0.,0.),([1.,0.,0.],1.,1.),([1.00001,0.,0.],1.00001,1.00001),([.1,0.,0.],.1,.1)]
for lo,hi,other_lo,other_hi in profiles:
 for j in range(3):
  m.put(body+0xf0+4*j,lo[j],'f');m.put(body+0x100+4*j,hi[j],'f')
  m.put(bounds+0x20+4*j,(other_hi[j]+other_lo[j])*.5,'f');m.put(bounds+0x30+4*j,(other_hi[j]-other_lo[j])*.5,'f')
 for velocity,signed,absolute in motions:
  for heading in (0,8192,49152):
   for delta in ([20.,0.,0.],[-20.,0.,0.],[0.,-20.,0.],[-2.,0.,0.],[3.,-20.,0.]):
    for target_velocity in ([0.,0.],[0.,-30.]):
     for target_forward in ([1.,0.],[0.,1.]):
      for has_shape in (True,False):
       position=[1000.,-500.,2.];other_position=[position[j]+delta[j]for j in range(3)]
       m.put(other+0x3c,shape if has_shape else 0)
       m.put(self+0x3b4,heading,'H');m.put(self+0x3d0,signed,'f');m.put(self+0x3d4,absolute,'f');m.put(self+0x460,0,'B')
       for base,values in [(self+0x60,position),(self+0x70,velocity),(coord+0x40,other_position),(coord+0x90,target_velocity),(coord+0x10,target_forward)]:
        for j,v in enumerate(values):m.put(base+4*j,v,'f')
       m.run(0x44f1b0,(self,out,out+4),edi=entry,budget=3000000)
       # Inputs use the actual472AA0 bounds, including its f32 rounding.
       actual_low=[];actual_high=[]
       for j in range(3):
        c=m.get(bounds+0x20+4*j,'f');r=m.get(bounds+0x30+4*j,'f')
        import struct
        actual_low.append(struct.unpack('<f',struct.pack('<f',c-r))[0]);actual_high.append(struct.unpack('<f',struct.pack('<f',c+r))[0])
       inputs=[*lo,*hi,*position,*velocity,signed,absolute,*actual_low,*actual_high,*other_position,*target_forward,*target_velocity]
       rows.append([heading,int(has_shape)]+[bits(v)for v in inputs]+[m.get(out),m.get(out+4)])
assert not m.events
text='// Actual44F1B0 with real shape3 getter,4650D0/464F20,math dispatch; no hooks.\nstatic constexpr unsigned kNativePhysicalObstacle[][31]={\n'
for row in rows:
 assert len(row)==31,len(row)
 text+=' {'+','.join('0x%08XU'%v for v in row)+'},\n'
text+='};\n'
target=root/'tests/ai_physical_obstacle_native.inc'
if args.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text('ascii')==text,'original physical obstacle fixture differs'
print('PASS actual physical obstacle fixture, rows='+str(len(rows)))
