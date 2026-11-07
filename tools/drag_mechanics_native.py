"""Bounded original drag constructor fields, clutch, RNG and nearest-lane cases."""
from pathlib import Path
import argparse,hashlib,json,struct
from hud254_trace import PE
from hud254_native import Machine
from handling_brake_compare import EXE_SHA,BUN_SHA
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_FPCW

def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def real(v):return struct.unpack('<f',struct.pack('<I',v))[0]
def f(v):return real(bits(v))
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--root',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--evidence',type=Path,required=True)
 a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
 assert hashlib.sha256(raw).hexdigest()==EXE_SHA and hashlib.sha256(bun).hexdigest()==BUN_SHA
 image=PE(raw);box,setup,out,world=0x1001000,0x1002000,0x1003000,0x1004000
 clutch=[];rng=[];lanes=[];offsets=[];constructors=[]
 for mode in range(4):
  for scale in (.4,.8,1.):
   for quality in (-1.,0.,.5,.85,1.):
    for request in (2,4):
     m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,0x7f)
     for off,v in ((0x18,setup),(0x60,0),(0x44,mode),(0x38,request),(0x68,bits(quality)),(0xdc,bits(scale))):m.write(box+off,v)
     m.write(setup+0x1c,bits(4.))
     m.run(0x46dfa0,(out,),{UC_X86_REG_ECX:box})
     expected=bits(f(.5+f(f(1/240)*real(m.integer(out)))))
     clutch.append([mode,request,bits(quality),bits(scale),bits(4.),bits(1/240),expected])
 p=0;configuration=None
 while p+8<=len(bun):
  tag,size=struct.unpack_from('<II',bun,p);p+=8
  assert p+size<=len(bun)
  if tag==0x34b00:assert configuration is None;configuration=bun[p:p+size]
  p+=size
 assert configuration is not None and len(configuration)==0x82c
 # Run the original constructor CONSUMER before testing RNG. This catches
 # swapped table fields, which seeding RNG with port-decoded bounds cannot.
 from world_body_trace import Image as RawImage
 from physics256_native import Machine as NativeMachine
 for context in (0,1,2,3):
  m=NativeMachine(RawImage(raw));m.allowed=[(0x476d6c,0x476e34),(0x476ec0,0x476ec7)]
  cfg,ctrl,phy,box2,eng,ai,vt,getter=(0x1100000+k*0x1000 for k in range(8))
  m.put(cfg+0x14,ctrl);m.put(ctrl+4,2);m.put(cfg+0x2c,ai);m.put(ai,vt);m.put(vt+0x58,getter)
  m.hook(getter,'supplied context getter',value=context)
  m.put(phy+0x48,eng);m.put(phy+0x4c,box2);m.put(box2+0xd8,1.,'f');m.put(box2+0xdc,1.,'f')
  m.put(0x78a344,1,'B');m.put(0x7361a8,0);m.put(0x777b4c,1);m.put(0x78a3fc,0)
  m.uc.mem_write(0x779ff8,configuration[0x380:0x380+10*0x54])
  m.run(0x476d6c,end=0x476e34,eax=cfg,ebp=phy)
  constructors.append([context,*[m.get(box2+at) for at in (0xc8,0xcc,0xd0,0xd4)]])
 for context in (0,1,2,3):
  row=0x380+context*0x54
  bounds=struct.unpack_from('<2f',configuration,row+0x2c)+struct.unpack_from('<2f',configuration,row+0x24)
  for seed in (1,12345,0x7ffffffe):
   m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,0x7f);m.write(0x7361f8,world);m.write(world+0x2d0,seed)
   for k,v in enumerate(bounds):m.write(box+0xc8+k*4,bits(v))
   m.run(0x46d760,registers={UC_X86_REG_ECX:box})
   rng.append([context,seed,*map(bits,bounds),m.integer(box+0x78),m.integer(box+0x7c),m.integer(world+0x2d0)])
 point,route,position=0x1005000,0x1006000,0x1007000
 for mask in (63,45):
  for y in (-8.,-6.,-3.,0.,3.,6.,8.):
   for first,last in ((255,255),(12,14)):
    m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,0x7f);m.write(0x77af30,route)
    m.write(point+0xc,bits(1.));m.write(position+4,bits(y))
    m.uc.mem_write(point+0x83,b'\0')
    for k,v in enumerate((-9.,-6.,-3.,0.,3.,6.)):
     m.uc.mem_write(point+0x52+2*k,struct.pack('<h',int(v*256)))
     m.uc.mem_write(route+0x270+k,bytes([(mask>>k)&1]))
    m.run(0x456e90,(0,0,first,last),{UC_X86_REG_EAX:point,UC_X86_REG_ECX:position})
    result=m.uc.reg_read(UC_X86_REG_EAX);result=result if result<0x80000000 else result-0x100000000
    lanes.append([mask,bits(y),first,last,result])
 for kind in (2,3):
  for speed in (0.,10.,25.,26.821636199951172,45.,60.):
   m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,0x7f)
   actor,context,bridge=0x1008000,0x1009000,0x100a000
   m.write(context,actor);m.write(actor+0x24,kind);m.write(actor+0x3d4,bits(speed));m.write(0x78a344,1)
   m.run(0x45c550,registers={UC_X86_REG_EAX:0,UC_X86_REG_ECX:context})
   # Observation-only x87 store after the authentic function returned.
   m.uc.mem_write(bridge,b'\xd9\x1d'+struct.pack('<I',out)+b'\xc3')
   m.run(bridge)
   offsets.append([bits(speed),kind,m.integer(out)])
 def array(name,rows):
  return 'const std::uint32_t '+name+'[][7]={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in rows)+'\n};\n'
 # Separate widths retain signed lane -1 without an unsigned conversion warning.
 lines='// Original476D6C/46DFA0/46D760/456E90, PC1.4 USA, x87=007F; constructor context getter supplied.\n'
 lines+=array('drag_clutch_native',clutch)
 lines+='const std::uint32_t drag_constructor_native[][5]={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in constructors)+'\n};\n'
 lines+='const std::uint32_t drag_shift_rng_native[][9]={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in rng)+'\n};\n'
 lines+='const std::int64_t drag_nearest_native[][5]={\n'+',\n'.join('{'+','.join(str(v) for v in r)+'}' for r in lanes)+'\n};\n'
 lines+='const std::uint32_t drag_offset_native[][3]={\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in offsets)+'\n};\n'
 a.output.write_text(lines,encoding='ascii');a.evidence.write_text(json.dumps(dict(executable_sha256=EXE_SHA,bundle_sha256=BUN_SHA,hooks=['constructor interface context getter only'],constructors=constructors,clutch=clutch,rng=rng,lanes=lanes,offsets=offsets,limits='Supplied component state and straight-road fixture, not a whole original race or live lane controller.'),indent=2)+'\n',encoding='ascii')
 print('PASS original clutch',len(clutch),'shift RNG',len(rng),'nearest lanes',len(lanes),'offsets',len(offsets),'constructors',len(constructors),'constructor getter supplied')
if __name__=='__main__':main()
