"""Original46D760 shared-world interleaving witness, supplied initial seed."""
from pathlib import Path
import sys,hashlib,json,struct,argparse
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
from handling_brake_compare import EXE_SHA,BUN_SHA

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
 a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
 assert hashlib.sha256(raw).hexdigest()==EXE_SHA and hashlib.sha256(bun).hexdigest()==BUN_SHA
 p=0;configuration=None
 while p+8<=len(bun):
  tag,n=struct.unpack_from('<II',bun,p);p+=8
  if tag==0x34b00:configuration=bun[p:p+n]
  p+=n
 assert configuration is not None and len(configuration)==0x82c
 m=Machine(Image(raw));m.allowed=[(0x46d760,0x46d830)]
 boxes=[0x1100000,0x1101000];world=0x1102000;m.put(0x7361f8,world);m.put(world+0x2d0,12345)
 bounds=[]
 for box,context in zip(boxes,(0,3)):
  row=0x380+context*0x54
  b=struct.unpack_from('<2I',configuration,row+0x2c)+struct.unpack_from('<2I',configuration,row+0x24);bounds.append(b)
  for j,v in enumerate(b):m.put(box+0xc8+4*j,v)
 rows=[]
 for actor in (0,1,0):
  before=m.get(world+0x2d0);m.run(0x46d760,ecx=boxes[actor])
  rows.append([actor,before,m.get(boxes[actor]+0x78),m.get(boxes[actor]+0x7c),m.get(world+0x2d0)])
 assert not m.events
 def array(name,data,width):return 'const std::uint32_t '+name+'[]['+str(width)+']={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in data)+'\n};\n'
 a.output.write_text('// Authentic original46D760, shared world RNG; supplied initial12345. No hooks.\n'+array('drag_world_bounds',bounds,4)+array('drag_world_draws',rows,5),encoding='ascii')
 a.evidence.write_text(json.dumps(dict(executable_sha256=EXE_SHA,bundle_sha256=BUN_SHA,bounds=bounds,rows=rows,hooks=[],limits='Original two gearbox consumers share a supplied world seed, order A/B/A; not proof of original scene initialization or global scheduler order.'),indent=2)+'\n',encoding='ascii')
 print('PASS original shared-world RNG A/B/A, 6 draws, no hooks')
if __name__=='__main__':main()
