"""Complete original587E90, with explicit flat-world and render-observer boundaries."""
from pathlib import Path
import argparse
import sys,struct,json,math,hashlib
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import Machine
ap=argparse.ArgumentParser();ap.add_argument('--retail-root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';image=Image(raw)
cases=[]
for longitudinal,lateral,slip,load,sliding,contact,material in [(20,0,0,3,False,True,1),(20,5,0,3,False,True,1),(20,0,4,3,False,True,1),(20,0,0,3,True,True,1),(0,0,10,3,True,True,1),(20,5,0,3,True,False,1),(20,5,0,3,True,True,13),(1,5,0,3,True,True,1)]:
 m=Machine(image);m.allowed=[(0x587e90,0x588164),(0x587e40,0x587e90),(0x567370,0x567490),(0x5abdb5,0x5abf00)]
 wheel,setup,owner,actor=0x1100000,0x1101000,0x1102000,0x1103000
 m.put(wheel+0x14,setup);m.put(wheel+0x20,owner);m.put(owner+4,0x1104000);m.put(setup+0x14,.21,'f');m.put(setup+0x18,0)
 for i in range(4):m.put(wheel+0x70+i*20,1.,'f')
 for at,value in [(0xf0,longitudinal),(0xf4,lateral),(0x168,math.hypot(slip,lateral)),(0x158,load),(0xa0,10.),(0xa4,20.)]:m.put(wheel+at,value,'f')
 m.put(wheel+0x150,material);m.put(wheel+0x154,int(sliding),'B');m.put(wheel+0x155,int(contact),'B')
 m.scalar_hook(0x440a40,'explicit-flat-ground-height',20)
 outputs=[]
 def collect(cpu):
  sp=cpu.reg('esp');v=lambda address:list(struct.unpack('<3f',cpu.uc.mem_read(address,12)))
  outputs.append({'point':v(cpu.get(sp+8)),'cross_section':v(cpu.get(sp+12)),'material':cpu.get(sp+16),'opacity':cpu.get(sp+20,'f')});cpu.ret(20)
 m.hook(0x589a30,'render-ribbon-submission-observer',callback=collect)
 m.run(0x587e90,(0,actor),eax=wheel)
 row={'inputs':[longitudinal,lateral,slip,load,sliding,contact,material],'outputs':outputs,'boundaries':m.events};cases.append(row);print(row['inputs'],outputs,flush=True)
args.out.write_text(json.dumps({'scope':'Original complete587E90 wheel skid producer; explicit flat world-query boundary and render submission observer. No complete world/pool parity.','exe_sha256':hashlib.sha256(raw).hexdigest(),'cases':cases},indent=2)+chr(10),encoding='ascii')
