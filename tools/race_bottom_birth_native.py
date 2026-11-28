"""Read-only original569A00/402D30 one-child motion controls."""
from pathlib import Path
import struct,json
from unicorn.x86_const import UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_ESP
import argparse,hashlib
from hud254_trace import PE
from race_particle_native_world import World
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=PE(raw);bank=(args.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bank).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
indices=[1,2,4]
def birth(part,seed,variant,pose,frames=0,dt=1/60):
 w=World(image);w.uc.mem_map(w.base+0x10000,0x30000)
 w.put(w.stack,w.done);w.reset(0x40b5e0,w.done,ESP=w.stack)
 w.system=0x78e9e0;w.empty(w.system);w.empty(w.system+8)
 car,fx=w.base+0xb800,w.base+0x30000;w.put(car+0x4d4,fx);w.put(0x734460,0)
 for k,v in enumerate((10.,20.,30.)):w.put(car+0x50+4*k,v,'f')
 matrix=((1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.),(0.,1.,0.,0.,-1.,0.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.),(1.,0.,0.,0.,0.,0.,1.,0.,0.,-1.,0.,0.,0.,0.,0.,1.))[pose]
 w.uc.mem_write(car+0x90,struct.pack('<16f',*matrix))
 for k,v in enumerate((0.,0.,0.) if not pose else (2.,3.,-1.)):w.put(car+0x70+4*k,v,'f')
 record=bytearray(bank[0x2034a0+indices[part]*544:0x2034a0+(indices[part]+1)*544])
 if variant:
  record[0x18]=1 if variant==1 else 0
  for at in (0x44,0x48,0x4c,0x50,0x54,0x84,0x88):struct.pack_into('<f',record,at,0.)
 source=w.source(0,0,seed=seed);w.uc.mem_write(w.get(source+0x1c),bytes(record))
 key=struct.unpack_from('<I',record,8)[0]
 place=next(bank[0x20f400+j*192:0x20f400+(j+1)*192] for j in range(90) if struct.unpack_from('<I',bank,0x20f400+j*192+12)[0]==key)
 w.uc.mem_write(source+0x30,place[0x30:0x70])
 marker=struct.unpack_from('<I',record,0x58)[0];head=fx+0x62c+8*marker;node=fx+0x800
 w.put(head,node);w.put(head+4,node);w.put(node,head)
 for k,v in enumerate((0.,0.,0.) if marker==0 else (-1.2,0.,-.3)):w.put(node+4+4*k,v,'f')
 head=fx+0x544+5*8;w.put(head,source);w.put(source,head)
 w.provide_particles(999)
 for off,value in ((0,w.done),(4,car),(12,1),(16,0)):w.put(w.stack+off,value)
 w.put(w.stack+8,.001,'f');w.uc.reg_write(UC_X86_REG_ESP,w.stack);w.uc.reg_write(UC_X86_REG_ECX,5)
 w.uc.emu_start(0x569a00,w.done,count=300000);assert w.uc.reg_read(UC_X86_REG_EIP)==w.done
 emitter=w.get(source+0x20);child=w.get(emitter+0x14);assert child!=emitter+0x14 and w.get(child)==emitter+0x14
 if frames:
  #403A60: disable further emission; only QueryPerformanceCounter is stubbed.
  w.empty(w.system+8);stub=w.base+0xfd00
  w.uc.mem_write(stub,bytes.fromhex('8b4c2404c70100000000c7410400000000b801000000c20400'))
  w.put(0x69717c,stub);w.put(0x7349c8,0);w.put(0x77a920,0)
  for tick in range(frames):
   w.put(w.stack,w.done);w.put(w.stack+4,w.system);w.put(w.stack+8,dt,'f')
   w.uc.reg_write(UC_X86_REG_ESP,w.stack)
   w.uc.emu_start(0x403a60,w.done,count=300000)
   assert w.uc.reg_read(UC_X86_REG_EIP)==w.done
 speed=struct.unpack('<H',w.uc.mem_read(child+0xc,2))[0]/128.
 direction=struct.unpack('<3b',w.uc.mem_read(child+0x1c,3))
 return dict(part=part,seed=seed,variant=variant,pose=pose,life=struct.unpack('<H',w.uc.mem_read(child+8,2))[0],cell=w.uc.mem_read(child+0x1f,1)[0],point=list(struct.unpack('<3f',w.uc.mem_read(child+0x10,12))),velocity=[v*speed/127. for v in direction],packed_speed=speed,packed_direction=direction)
rows=[birth(p,s,0,q) for p in range(3) for s in (1,121,0x1234abcd) for q in (0,1,2)]
rows += [birth(1,121,v,q) for v in (1,2) for q in (0,1,2)]
for r in rows[-4:]:print(r)
assert all(r['velocity'][2]==0 for r in rows if r['variant']==1 and r['pose']!=2)
assert all(r['velocity'][2]==4 for r in rows if r['variant']==2 and r['pose']!=2)
(args.out/'bottom-birth-native.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
lines=['// Original569A00/402D30; same body pose, marker, carrier and source seed.',
 '// Variant0 stock;1 planar zero tilt/spread/inherit;2 same inputs with planar flag0.',
 'struct NativeBottomBirth { unsigned part,seed,variant,pose,life,cell; double point[3],velocity[3]; };',
 'inline const NativeBottomBirth kNativeBottomBirths[]{']
for r in rows:
 fmt=lambda x:','.join('%.17g'%v for v in x)
 lines.append('    {%d,0x%08xU,%d,%d,%d,%d,{%s},{%s}},'%(r['part'],r['seed'],r['variant'],r['pose'],r['life'],r['cell'],fmt(r['point']),fmt(r['velocity'])))
lines+=['};',''];(args.out/'bottom_particle_birth_native.inc').write_text('\n'.join(lines),encoding='ascii')
print('PASS original33 births: planar flag, stock flash/smoke/spark, yaw and rolled source controls')

motion=[]
for part in range(3):
 for pose in range(3):
  for frames in (1,3,8,16):
   for dt in (1/60,1/30):
    r=birth(part,121,0,pose,frames,dt);r.update(frames=frames,dt=dt);motion.append(r)
(args.out/'bottom-motion-native.json').write_text(json.dumps(motion,indent=2)+'\n',encoding='ascii')
lines=['// Original403A60; continued stock births, only profiling time query stubbed.',
 'struct NativeBottomMotion { unsigned part,pose,frames,life,cell; double dt,point[3],velocity[3]; };',
 'inline const NativeBottomMotion kNativeBottomMotions[]{']
for r in motion:
 fmt=lambda x:','.join('%.17g'%v for v in x)
 lines.append('    {%d,%d,%d,%d,%d,%.17g,{%s},{%s}},'%(r['part'],r['pose'],r['frames'],r['life'],r['cell'],r['dt'],fmt(r['point']),fmt(r['velocity'])))
lines+=['};',''];(args.out/'bottom_particle_motion_native.inc').write_text('\n'.join(lines),encoding='ascii')
print('PASS original72 later-frame controls, stock drag/gravity/animation at30Hz and60Hz')


assert (args.retail_root/'speed.exe').read_bytes()==raw
