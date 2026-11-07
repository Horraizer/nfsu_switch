"""Authenticated PC1.4 USA kind0/unbacked-kind4 response witnesses.

Supplied primitive geometry, pose, controller state and physical bounds.
Original46BAE0 and465D00 execute; only the later audiovisual tail is omitted.
This is not a full world or natural contact-admission replay.
"""
from pathlib import Path
from hashlib import sha256
import json,math,sys
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
import contact256_native as n
from unicorn.x86_const import UC_X86_REG_EIP
source=(Path(sys.argv[1])/'speed.exe').read_bytes()
assert sha256(source).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(source);rows=[]
for speed in (5,20,50):
 for side in (0,.7):
  for state in (0,1):
   for world_first in (False,True):
    for up in (1.,0.,.7,.70001):
     offset=(1.9,side,.2);m=n.fixture(image,offset,(speed,0,0),(0,0,0),state=state)
     obj,node,ride=n.CONFIG+0x3000,n.CONFIG+0x4000,n.CONFIG+0x6000
     wb,cb,cr,ca=(n.BODY_A,n.BODY_B,n.RIGID_B,n.ACTOR_B) if world_first else (n.BODY_B,n.BODY_A,n.RIGID_A,n.ACTOR_A)
     for rigid,actor,pos,vel in ((n.RIGID_A,n.ACTOR_A,offset if world_first else (0,0,0),(0,0,0) if world_first else (speed,0,0)),
                                (n.RIGID_B,n.ACTOR_B,(0,0,0) if world_first else offset,(speed,0,0) if world_first else (0,0,0))):
      n.vector(m,rigid+0x20,pos);n.vector(m,actor+0x60,pos);n.vector(m,rigid+0xa0,vel);n.vector(m,actor+0x70,vel);n.vector(m,rigid+0x70,[2*v for v in vel])
     spin=math.sqrt(1-up*up)
     axes=((1,0,0),(0,up,spin),(0,-spin,up))
     for k,axis in enumerate(axes):n.vector(m,cr+0x30+16*k,axis);n.vector(m,ca+0x90+16*k,axis)
     m.put(wb,0x6b8d88);m.put(wb+0x15,4,'B');m.put(wb+0x60,0);m.put(wb+0x140,obj)
     m.put(obj,0x6ba65c);m.put(obj+0x1c,node);n.vector(m,node+0x40,offset);n.matrix(m,node+0x50)
     m.put(ca+0x20,ride);m.put(ride+0x14,-.7,'f');m.put(ride+0x44,.7,'f')
     n.vector(m,n.PAIR+0x20,(.95,side*.5,.1));n.vector(m,n.PAIR+0x30,(.95,side*.5,.1))
     m.run(0x46b8c0,edi=n.JOB,eax=n.NORMAL,ecx=n.POINT)
     geometry=dict(normal=n.vector(m,n.NORMAL),point=n.vector(m,n.POINT))
     m.hook(0x4658b5,'declared audiovisual tail omitted after465D00',callback=lambda x:x.uc.reg_write(UC_X86_REG_EIP,0x465cf2))
     m.run(0x46bae0,(n.JOB,),budget=3000000)
     rows.append(dict(speed=speed,side=side,state=state,world_first=world_first,up=up,axes=axes,geometry=geometry,
        momentum=n.vector(m,cr+0x70),angular=n.vector(m,cr+0x80),car_state=m.get(ca+0x38),
        record_gate=m.get(n.RECORD+0x10),magnitude=m.get(n.RECORD+0x60,'f'),seed=m.get(n.CONFIG+0x12d0)))
print(json.dumps(rows,indent=2))
