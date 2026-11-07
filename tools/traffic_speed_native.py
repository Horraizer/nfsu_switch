"""Actual583910 on declared forward compact-road inputs, no semantic hooks.
This is a numeric publication witness, not a TrafficController or race replay.
"""
from pathlib import Path
import sys,struct,hashlib,argparse
sys.dont_write_bytecode=True;root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');args=p.parse_args()
from world_body_trace import Image
from physics256_native import Machine,bits
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)]
owner,actor,descriptor,context,chain,action,route,cells,out=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000,0x1106000,0x1108000,0x110a000
m.put(owner,0x6b96b0);m.put(owner+4,actor);m.put(owner+0x3d8,descriptor);m.put(owner+0x3e0,context);m.put(owner+0x3e4,chain);m.put(owner+0x3f8,action);m.put(context+0x388,0,'B');m.put(chain+4,4);m.put(0x77af30,route)
profiles=[([0.,10.,20.,40.],[0.,0.,0.,0.],[15,35,55,20]),([0.,10.,20.,40.],[0.,-2.,-2.,-2.],[80,80,80,80]),([0.,10.,25.,50.],[0.,0.,0.,-2.],[80,80,80,80]),([0.,5.,15.,30.],[0.,-.1,-.1,-.1],[20,30,40,50]),([0.,5.,15.,30.],[0.,-.10000001,-.10000001,-.10000001],[20,30,40,50])]
for i in range(4):
 point=cells+i*0x80;m.put(chain+8+4*i,point);m.put(point,0x6c8f90);m.put(point+0x5c,0,'B');m.put(point+0x5e,i,'h')
rows=[]
for profile,(distances,grades,speeds)in enumerate(profiles):
 bounds=[123.,-321.,20.,-7.] if profile%2==0 else [0.,0.,0.,0.]
 for i in range(4):
  point=cells+i*0x80;m.put(point+0x58,distances[i],'f');m.put(point+0x30,bounds[i],'f');m.put(route+0x27c+i*0x88+0x30,grades[i],'f')
  for cat in (2,11,16):m.put(route+0x27c+i*0x88+0x6a+cat,speeds[i],'B')
 for category in (2,11,16):
  m.put(descriptor+8,category)
  for distance in (0.,8.,24.):
   first=next((i for i,v in enumerate(distances)if v>=distance),3)
   m.put(context+0x38,first);m.put(context+0x18,distance,'f')
   for speed in (0.,20.,40.):
    m.put(actor+0x3d4,speed,'f')
    for cap in (10.,24.5865,40.):
     m.put(owner+0x4dc,cap,'f')
     for stopped in (False,True):
      m.put(owner+0x4d1,int(stopped),'B')
      for use_route,values in [(True,(0.,0.,0.)),(False,(6.,.2,2.)),(False,(100.,1.5,.5))]:
       m.put(action+0x1f0,int(use_route),'B')
       for i,value in enumerate(values):m.put(action+0x1e4+4*i,value,'f')
       m.run(0x583910,(out+4,out+8),esi=owner,edi=out)
       rows.append([profile,category,first,bits(distance),bits(speed),bits(cap),int(stopped),int(use_route)]+[bits(v)for v in values]+[m.get(out+4*i)for i in range(3)])
assert not m.events
lines=['// Actual583910; declared compact forward cells, original cap/curve/road getters.','static constexpr unsigned kNativeTrafficSpeedProfiles[][16]={']
for index,(d,g,s)in enumerate(profiles):
 b=[123.,-321.,20.,-7.] if index%2==0 else [0.,0.,0.,0.]
 lines.append(' {'+','.join('0x%08XU'%v for v in [bits(v)for v in d+g+b]+s)+'},')
lines+=['};','static constexpr unsigned kNativeTrafficSpeed[][14]={']
for row in rows:lines.append(' {'+','.join('0x%08XU'%v for v in row)+'},')
lines+=['};'];target=root/'tests/traffic_speed_native.inc';text='\n'.join(lines)+'\n'
if args.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text('ascii')==text,'original traffic speed fixture differs'
print('PASS actual traffic numeric speed publication, rows='+str(len(rows)))
