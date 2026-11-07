"""Original45FAF0 copies a raw action path before459980 normalizes it.

Compare the real setter to the independently exercised publication kernel
and verify its source bytes survive. Supplied paths, not a natural race.
No semantic hooks, unchanged retail instructions and no file mutation.
"""
from pathlib import Path
import sys,hashlib,argparse
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
from ai_controller_cadence_vectors import publish
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail',type=Path)
a=p.parse_args();raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw)
cases=[[],[(3.,2.,1.)],[(3.,2.,1.),(4.,5.,6.)],[(0.,0.,0.)]*4,
 [(0.,0.,0.),(1.,0.,0.),(1.,11.,0.)],
 [(100.,-300.,24.),(101.,-303.,24.1),(101.1,-303.2,24.11),(125.,-310.,25.)],
 [(float(i*i),float(i%3)*7,float(i)*.25)for i in range(20)]]
for points in cases:
 expected=publish(image,points);m=Machine(image);source,driver=0x1150000,0x1160000;path=driver+0x10
 m.allowed=[(0x45faf0,0x45fb27),(0x459980,0x459abb),(0x459870,0x45997d),
            (0x583f20,0x583fe8),(0x4387e0,0x4387f7),(0x674898,0x67490e)]
 m.put(source,len(points),'B');m.put(source+0x1ac,77.,'f')
 m.put(source+0x1a0,2,'B');m.put(source+0x1a4,12345);m.put(source+0x1a8,11111)
 for i,point in enumerate(points):
  for k,value in enumerate(point):m.put(source+16+16*i+4*k,value,'f')
 before=bytes(m.uc.mem_read(source,0x1c0));m.run(0x45faf0,(source,),ecx=driver,budget=10000)
 actual=dict(points=[[m.get(path+16+16*i+4*k)for k in range(3)]for i in range(len(points))],
             length=m.get(path+0x1ac),cursor=m.get(path+0x1a0,'B'),
             cursor_tick=m.get(path+0x1a4),prediction_tick=m.get(path+0x1a8))
 assert actual==expected,'driver setter publication differs'
 assert bytes(m.uc.mem_read(source,0x1c0))==before,'setter changed action retention bytes'
 assert not m.events
print('PASS original human driver copy/publication, '+str(len(cases))+' cases, no hooks')
