"""Compare unmodified native PC wheel leaves with port code on archived GC inputs.

The explicit common owner speed and step do not assert a whole GC callback.
This checks prepare, limit, derivative, history, Euler and auxiliary epochs
under the original packed PC transform. It never starts a game or substitutes
measured coefficients into production.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib

from world_body_trace import Image
from physics256_native import Machine,bits
from launch_traction_boundary import PC_HASH
from unicorn.x86_const import UC_X86_REG_CR4

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--retail',type=Path,required=True)
parser.add_argument('--capture',type=Path,required=True)
parser.add_argument('--state',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--owner-speed',type=float,default=0)
parser.add_argument('--step',type=float,default=.0041)
parser.add_argument('--max-clock',type=int,default=700)
parser.add_argument('--compiler',default='c++')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
output=args.out.resolve()
try:relative=output.relative_to(root)
except ValueError:parser.error('output must stay in this checkout')
if not relative.parts or not all(p.isascii() for p in relative.parts) or output.exists():
    parser.error('fresh ASCII output inside checkout required')
retail=args.retail.resolve()
if retail==output or retail in output.parents:parser.error('output must stay outside retail')
if not 0<args.step<=.0044 or not abs(args.owner_speed)<1000 or args.max_clock<0:
    parser.error('finite physical inputs required')
pc=(retail/'speed.exe').read_bytes()
if hashlib.sha256(pc).hexdigest()!=PC_HASH:parser.error('unsupported PC executable')
image=Image(pc)
f=lambda word:struct.unpack('<f',struct.pack('<I',word))[0]
F32=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]

CPP=r"""#include "nfsu/player_powertrain.h"
#include <iomanip>
#include <iostream>
int main(){
 using namespace nfsu;
 unsigned k,contact,locked,direction,surface;
 float dt,rolling;
 std::cout<<std::setprecision(17);
 while(std::cin>>k){
  VehicleHandling h;PlayerWheelWork w;VehicleVec3 velocity,along,side;
  std::cin>>h.wheels[k].radius>>h.tyres[k/2].slip_scale>>h.tyres[k/2].lateral_load_scale
   >>h.tyres[k/2].lateral_scale>>h.tyres[k/2].static_friction>>h.tyres[k/2].sliding_friction
   >>h.service_brake>>h.parking_brake>>w.spin>>w.history_torque>>w.torque>>w.owner_grip
   >>w.friction_scale>>w.load>>w.owner_speed>>w.service>>w.parking>>direction>>contact>>locked>>surface
   >>velocity.x>>velocity.y>>velocity.z>>along.x>>along.y>>along.z>>side.x>>side.y>>side.z>>dt>>rolling;
  w.contact=contact;w.locked=locked;w.spin_direction=direction;
  if(!std::cin||!player_surface_factors(surface,w.surface))return 2;
  w.velocity_long=player_axis_projection(velocity,along);
  w.velocity_lat=player_axis_projection(velocity,side);
  player_wheel_prepare(h,k,w);
  std::cout<<w.velocity_long<<' '<<w.velocity_lat<<' '<<w.torque<<' '<<w.spin<<' '<<w.slip
   <<' '<<w.cached_slip<<' '<<w.force_long<<' '<<w.force_lat<<' '<<w.locked<<' '<<w.sliding;
  player_wheel_limit(h,k,w);
  const float rate=player_wheel_acceleration(h,k,w,dt,rolling);
  player_wheel_integrate(w,rate,dt);
  std::cout<<' '<<w.force_long<<' '<<w.force_lat<<' '<<w.friction_usage<<' '<<rate
   <<' '<<w.history_torque<<' '<<w.spin<<' '<<w.spin_direction<<'\n';
 }
 return std::cin.eof()?0:3;
}
"""


def take(case):
 cache=args.state.read_bytes()
 length=int.from_bytes(cache[:4],'little');meta=json.loads(cache[4:4+length])
 raw=zlib.decompress(cache[4+length:]);assert hashlib.sha256(raw).hexdigest()==meta['sha256']
 metadata=json.loads((args.capture/'result.json').read_bytes())
 if metadata['case']!=meta['case'] or metadata['observed'].get('grid_cache')!=meta['sha256']:
  raise ValueError('capture does not bind this original state')
 origin=raw.index(b'GNDE69\0\0')
 def memory(address,size):
  assert 0x80000000<=address<=address+size<=0x81800000
  return raw[origin+address-0x80000000:origin+address-0x80000000+size]
 def floats(address,size):return struct.unpack('>%df'%(size//4),memory(address,size))
 rows=[]
 for line in (args.capture/'wheel-state.jsonl').read_bytes().splitlines():
  record=json.loads(line)
  if record[0]>args.max_clock:break
  for k,w in enumerate(record[10]):
   params=floats(w[0x14//4],0x30);tyre=floats(w[0x18//4],0x20);controls=floats(w[0x1c//4],0x20)
   assert f(w[0x17c//4])==1 and w[0x150//4]<29
   m=Machine(image)
   m.uc.reg_write(UC_X86_REG_CR4,m.uc.reg_read(UC_X86_REG_CR4)|0x200)
   m.allowed=[(0x587330,0x587822),(0x587830,0x587A91),(0x587170,0x58721E),
      (0x586DA0,0x586F0D),(0x46AA40,0x46AC08),(0x586710,0x586762),
      (0x564B10,0x564F20),(0x565230,0x5652FF),(0x5ABDAF,0x5ABF00),
      (0x583EA0,0x583F18),(0x674898,0x67490D),(0x4387E0,0x4387F6),(0x587220,0x587330),(0x575940,0x575945),(0x5EBCE3,0x5EBDB7),(0x5ECE7A,0x5ECFAE)]
   wheel,wp,tp,cp,owner,out,system,vt=[0x1100000+0x1000*i for i in range(8)]
   for i,word in enumerate(w):m.put(wheel+4*i,word)
   for ptr,values in ((wp,params),(tp,tyre),(cp,controls)):
    for i,value in enumerate(values):m.put(ptr+4*i,value,'f')
   for at,value in ((0,0x6B9544),(0x14,wp),(0x18,tp),(0x1C,cp),(0x20,owner)):
    m.put(wheel+at,value)
   # Byte flags are not swapped like scalar words when crossing endianness.
   m.uc.mem_write(wheel+0x154,w[0x154//4].to_bytes(4,'big'))
   m.put(owner+0x3C0,0)
   m.put(owner+0x390,args.owner_speed,'f') # Explicit supplied owner context.
   m.put(0x6D4AC4,0x5EBCE3)
   m.put(0x6D4ACC,0x5ECE7A)
   m.put(0x73AD34,0)
   m.run(0x587330,(0,0),esi=wheel)
   prepared=[m.get(wheel+o,'f') for o in (0x108,0x28,0x160,0x164,0x118,0x11C)]
   prepared.extend((m.get(wheel+0x10C),m.get(wheel+0x154,'B')))
   m.run(0x587830,(k,),end=0x587A91,eax=wheel)
   limited=[m.get(wheel+o,'f') for o in (0x118,0x11C,0x168)]
   dt=F32(args.step)
   m.put(0x6F0738,dt,'f')
   before=bytes(m.uc.mem_read(wheel,0x184))
   m.run(0x587170,(out,),ecx=wheel)
   rate=m.get(out+4,'f');history=m.get(wheel+0x114,'f')
   m.uc.mem_write(wheel,before)
   m.put(system,vt);m.put(vt+4,0x46ABD0)
   m.put(system+0xC,wheel+4);m.put(wheel+4,system+0xC)
   m.run(0x46AA40,(system,bits(dt)))
   final=[m.get(wheel+0x28,'f'),m.get(wheel+0x110)]
   assert not m.events
   byteflags=w[0x154//4].to_bytes(4,'big')
   inp=[k,params[4],*tyre[:5],controls[1],controls[2],
        f(w[10]),f(w[0x114//4]),f(w[0x108//4]),f(w[0x178//4]),f(w[0x180//4]),
        f(w[0x158//4]),args.owner_speed,f(w[0x170//4]),f(w[0x174//4]),w[0x110//4],byteflags[1],w[0x10C//4],w[0x150//4],
        *[f(x) for x in w[0xF0//4:0xFC//4]],*[f(x) for x in w[0x70//4:0x7C//4]],
        *[f(x) for x in w[0x80//4:0x8C//4]],dt,f(w[0x16C//4])]
   rows.append(dict(case=case,clock=record[0],wheel=k,input=inp,native=prepared+limited+[rate,history]+final,
                    state_sha256=meta['sha256']))
 return rows

output.mkdir(parents=True)
source=output/'wheel-leaf.cpp'
source.write_bytes(CPP.encode('ascii'))
binary=output/'wheel-leaf.exe'
sources=[root/'src'/name for name in ('player_powertrain.cpp','vehicle.cpp','sha256.cpp','nitrous.cpp','drift.cpp')]
command=[args.compiler,'-std=c++17','-O1','-ffp-contract=off','-fno-fast-math','-I',root/'include',source,*sources,'-o',binary]
build=subprocess.run(command,cwd=root,capture_output=True)
(output/'build.log').write_bytes((build.stdout+build.stderr).decode('utf8','backslashreplace').encode('ascii','backslashreplace'))
if build.returncode:raise RuntimeError('portable wheel bridge did not build')
rows=take(args.capture.name)
if not rows:raise ValueError('no captured full-mover wheel inputs')
inputs=''.join(' '.join(format(x,'.17g') for x in row['input'])+'\n' for row in rows)
run=subprocess.run([binary],input=inputs,capture_output=True,text=True,check=True)
if run.stderr:raise RuntimeError('portable bridge reported an error')
port=[list(map(float,line.split())) for line in run.stdout.splitlines()]
if len(port)!=len(rows) or any(len(line)!=17 for line in port):
    raise RuntimeError('fresh portable rows incomplete')
names=['torque','spin','slip160','slip164','long_force','lateral_force','locked','sliding',
       'limited_long_force','limited_lateral_force','usage','angular_accel','history_torque','completed_spin','direction']
differences=[]
for row,actual in zip(rows,port):
 row['portable']=actual
 for name,native,value in zip(names,row['native'],actual[2:]):
  if native!=value:differences.append([row['clock'],row['wheel'],name,native,value])
report=dict(pc_sha256=PC_HASH,tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            capture_sha256=hashlib.sha256((args.capture/'wheel-state.jsonl').read_bytes()).hexdigest(),
            source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode('ascii').strip(),
            sources={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
            dispatch='6D4AC4 -> original packed5EBCE3; scalar fallback not selected',
            owner_speed=args.owner_speed,step=args.step,rows=rows,differences=differences,hooks=[],
            boundary='Supplied unchanged GC wheel inputs; explicit common owner speed/step and driving style0; original PC prepare, limit, derivative, full Euler and auxiliary update. No body/gearbox or GC whole-callback parity claim.')
(output/'report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode('ascii'))
if hashlib.sha256((retail/'speed.exe').read_bytes()).hexdigest()!=PC_HASH:raise RuntimeError('retail input changed')
if differences:raise AssertionError('native/portable wheel differences: see report.json')
print('PASS:',len(rows),'exact native/portable wheel rows, no semantic hooks')
