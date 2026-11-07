from pathlib import Path
import sys,hashlib,math,json
from world_body_trace import Image
import contact256_native as n
if len(sys.argv)!=3:raise SystemExit('usage: contact_callback_native.py executable output-json')
raw=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);original_fixture=n.fixture;rows=[]
for name,yaw,pitch,support,gate,speed in [('identity',0,0,4,0,50),('yaw',.5,0,4,0,50),('pitch',0,.35,4,0,50),('airborne',.5,.35,0,0,50),('mixed_support',.5,.35,2,0,50),('recovered_record',0,0,4,1,10)]:
 c,s,cp,sp=math.cos(yaw),math.sin(yaw),math.cos(pitch),math.sin(pitch)
 axes=[[c*cp,s*cp,-sp],[-s,c,0],[c*sp,s*sp,cp]]
 def rotate(v):return [sum(v[j]*axes[j][k] for j in range(3)) for k in range(3)]
 def fixture(*args,**kw):
  m=original_fixture(*args,**kw)
  for i,(actor,rigid) in enumerate(((n.ACTOR_A,n.RIGID_A),(n.ACTOR_B,n.RIGID_B))):
   for base in (actor+0x90,rigid+0x30):
    for j in range(3):
     for k in range(3):m.put(base+j*16+k*4,axes[j][k],'f')
   m.put(actor+0x3cc,support if i==0 else 4)
   physical=n.CONFIG+0x300+i*0x100;m.put(actor+0x20,physical)
   m.put(physical+0x14,-1.,'f');m.put(physical+0x44,1.,'f')
  m.put(n.RECORD+0x10,gate)
  return m
 n.fixture=fixture
 try:
  row=n.contact_case(im,name,rotate((2.0005,.7,0)),rotate((speed,0,0)),rotate((-speed,0,0)))
  row['callback_input']={'axes':axes,'support':[support,4],'bounds':[[-1,1],[-1,1]],'gate':bool(gate),'seed':12345}
  rows.append(row);print(name,'OK', [b['state'] for b in row['after']])
 except Exception as e:print(name,'UNKNOWN',str(e));rows.append({'name':name,'error':str(e)})
n.fixture=original_fixture
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2),encoding='ascii')

if any('error' in row for row in rows):raise SystemExit(1)
