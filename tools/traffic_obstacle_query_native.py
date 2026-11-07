"""Actual44F470, including the real44F1B0 geometry forecast.
Neighborhood records, raw boxes and fresh world coordinates are declared
inputs. No supplied physical-query returns or semantic hooks. This is not
proof of the57A910 registration/publishing sequence or a natural traffic run.
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

m.put(self+0x70,20.,'f');m.put(self+0x3d0,20.,'f');m.put(coord+0x40,-20.,'f')
ctx=0x1109000
m.put(ctx,self);m.put(ctx+4,3)
profiles=[[(35.,0.,0.),(15.,0.,0.)],[(35.,3.,-3.),(15.,0.,0.)],[(8.,0.,0.),(30.,0.,0.)],[(100.,0.,0.),(50.,0.,0.)]]
rows=[]
for profile,candidates in enumerate(profiles):
 for category in (0,11,16):
  for self_index in (0,1,2):
   m.put(ctx+0x388,self_index,'B')
   for offset in (-3.,0.,3.):
    for clearance in (0.,11.,20.):
     for absolute in (0.,20.):
      m.put(self+0x3d4,absolute,'f')
      for relative in (-10.,-.01,0.):
       for i in range(3):
        at=ctx+8+i*0x38
        # Entries below self are selected in reverse array order.
        d,lat,lane=candidates[i]if i<self_index else(0.,0.,0.)
        for off,value in [(4,relative),(8,1. if i==self_index else .8),(12,2.),(16,d),(20,lat)]:m.put(at+off,value,'f')
        for j in range(6):m.put(at+24+4*j,lane+float(j)*.25,'f')
        m.put(at+52,entry+i*0x400);m.put(entry+i*0x400+0x1d4,obj)
       m.put(self+0x460,0,'B')
       m.run(0x44f470,(category,bits(offset),bits(clearance),out,out+4),ecx=ctx,budget=3000000)
       rows.append([profile,category,self_index,bits(offset),bits(clearance),bits(absolute),bits(relative),m.reg('eax'),m.get(out),m.get(out+4)])
assert not m.events
text='// Actual44F470 with actual44F1B0 physical query and declared neighborhood records.\nstatic constexpr unsigned kNativeObstacleQuery[][10]={\n'
for row in rows:text+=' {'+','.join('0x%08XU'%v for v in row)+'},\n'
text+='};\n'
target=root/'tests/ai_obstacle_query_native.inc'
if args.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text('ascii')==text,'original obstacle query fixture differs'
print('PASS actual obstacle aggregation, rows='+str(len(rows)))
