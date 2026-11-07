"""Original neutral target, neutral->first request and pulse on bounded state."""
import argparse,hashlib,json,struct
from pathlib import Path
from world_body_trace import Image
from physics256_native import Machine,STACK
from handling_brake_compare import EXE_SHA,BUN_SHA

def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def real(v):return struct.unpack('<f',struct.pack('<I',v))[0]
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for n in ('root','output','evidence'):ap.add_argument('--'+n,type=Path,required=True)
 a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
 assert hashlib.sha256(raw).hexdigest()==EXE_SHA and hashlib.sha256(bun).hexdigest()==BUN_SHA
 image=Image(raw);rows=[];targets=[]
 box,setup,engine,up,down=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000
 # The constructor request-table prefix and neutral factory adjustment both
 # execute at their real addresses. Stop before the forward crossing search.
 for drive in (0.,.5,1.):
  for idle,redline,fraction in ((1000.,7000.,.8),(850.,6500.,.75)):
   m=Machine(image);m.allowed=[(0x46d1c0,0x46d208),(0x46d43b,0x46d45d)]
   m.put(setup,fraction,'f');m.put(engine,idle,'f');m.put(engine+4,redline,'f');m.put(engine+8,8000.,'f')
   m.run(0x46d1c0,(setup,up),end=0x46d208,edi=engine,edx=down)
   m.put(box+0x84,m.get(up+4));m.put(box+0x1c,0x6f0fc0+int(drive*2)*0x6c)
   m.run(0x46d43b,end=0x46d45d,esi=box)
   target=m.get(box+0x84);targets.append([bits(drive),bits(idle),bits(redline),bits(fraction),target])
 # Real46E5B0 request must call46E940 for neutral->first, exactly once.
 # Sample both sides of the launch window, all source categories/scales.
 for category in (0,3):
  for scale in (1.,1.5,3.):
   for rpm in (2500.,4500.,5500.,5800.,5801.,5850.,6500.,7500.):
    m=Machine(image);m.allowed=[(0x46e5b0,0x46ea2b),(0x46dec3,0x46df87),(0x46dc98,0x46dcc7)]
    for off,v in ((0x3c,1),(0x38,1),(0x44,3),(0x60,0),(0xe0,category),(0xd8,bits(scale)),(0x84,bits(5800.)),(0x20,engine)):
     m.put(box+off,v)
    m.put(engine+0x14,rpm*real(struct.unpack('<I',image.read(0x6ccab4,4))[0]),'f')
    m.put(0x78a344,1);m.put(0x73ad34,40000)
    m.run(0x46e5b0,(2,),esi=box)
    actual_rpm=bits(m.get(engine+0x14,'f')*real(struct.unpack('<I',image.read(0x6ccb78,4))[0]))
    requested=m.get(box+0x38);quality=m.get(box+0x68)
    assert requested==2 and 0x46e940 in m.visited
    # Supply completed clutch mode3 to the authentic existing common tail.
    # Its epilogue pops five saved words before the return address.
    m.put(engine+0x80,1.,'f')
    m.run(0x46dc98,end=0x46dcc7,esi=box)
    m.run(0x46dec3,(0,0,0,0,0x1000000),esi=box)
    rows.append([actual_rpm,category,bits(scale),requested,quality,m.get(engine+0x80)])
 out='// Original46D1C0/46D43B/46E5B0/46E940/46DEC3, PC1.4 USA, no hooks.\n'
 for name,data in [('drag_launch_targets',targets),('drag_launch_requests',rows)]:
  out+='const std::uint32_t '+name+'[]['+str(len(data[0]))+']={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in data)+'\n};\n'
 a.output.write_text(out,encoding='ascii')
 a.evidence.write_text(json.dumps(dict(executable_sha256=EXE_SHA,bundle_sha256=BUN_SHA,hooks=[],targets=targets,requests=rows,limits='Supplied factory/gearbox/countdown-release state, not original whole-controller or whole-race execution.'),indent=2)+'\n',encoding='ascii')
 print('PASS original launch targets',len(targets),'neutral requests and pulse',len(rows),'hooks0')
if __name__=='__main__':main()
