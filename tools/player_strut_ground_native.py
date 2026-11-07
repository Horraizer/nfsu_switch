from pathlib import Path
import sys,struct,json,hashlib,argparse
from hud254_trace import PE
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description="Bounded original587070 static-road strut height witness")
ap.add_argument('--root',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
ap.add_argument('--evidence',type=Path,required=True)
args=ap.parse_args()
for output in (args.output,args.evidence):
 assert output.resolve()!=args.root.resolve() and args.root.resolve() not in output.resolve().parents
 assert not output.exists(),"Refusing to overwrite witness output"
data=(args.root/'speed.exe').read_bytes()
assert hashlib.sha256(data).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=PE(data);rows=[]
for slope in (0.,.25,-.25):
 for z in (-1.,-.25,.25,1.25,4.):
  m=Machine(image)
  m.allowed=[(0x587070,0x58716a),(0x440a40,0x440cdc),(0x4400e0,0x4401f6),
   (0x577060,0x577250),(0x40cb70,0x40cb7b),(0x441d80,0x441ecc),(0x4387e0,0x4387f6),
   (0x674898,0x67490d),(0x564b10,0x564c00),(0x564cd0,0x564f20),(0x565230,0x565300),
   (0x5abd00,0x5abf00),(0x583ea0,0x583f18),(0x567370,0x5673dc),(0x1101500,0x1101507)]
  strut,owner,cache,poly,point,normal,material,body,result,tramp=0x1100000,0x1100200,0x1100400,0x1100800,0x1101000,0x1101100,0x1101200,0x1101300,0x1101400,0x1101500
  m.put(strut+0x20,owner);m.put(owner+4,cache);m.put(owner+8,owner+0x10)
  m.put(0x73b14c,0x73b14c);m.put(0x6d4ac4,0x5abdb5)
  m.uc.mem_write(cache,struct.pack('<4f',-100,-100,100,100));m.put(cache+0x2a4,1);m.put(cache+0x20,poly)
  m.put(poly+9,4,'B');m.put(poly+11,0,'B');m.uc.mem_write(poly+0x14,struct.pack('<4f',-5,-5,5,5))
  for k,(x,y) in enumerate([(-5,-5),(5,-5),(5,5),(-5,5)]):m.uc.mem_write(poly+0x30+16*k,struct.pack('<3f',x,y,slope*x))
  # Original vector normalization, not host-math theoretical unit normals.
  m.uc.mem_write(normal,struct.pack('<3f',-slope*100.,0,100.))
  m.run(0x567370,ecx=poly+0x70,edx=normal);m.put(poly+0x7c,0.,'f')
  m.uc.mem_write(point,struct.pack('<3f',1,0,z));m.uc.mem_write(body+0x20,struct.pack('<3f',0,0,1))
  m.uc.mem_write(tramp,b'\xd9\x1d'+struct.pack('<I',result)+b'\xc3')
  m.run(0x587070,(strut,material,body),edi=point,esi=normal);m.run(tramp)
  assert not m.events
  rows.append({'slope':slope,'point':[1,0,z],'gap_bits':m.get(result),'material':m.get(material),'normal_bits':[m.get(normal+4*j) for j in range(3)],'cached':bool(m.get(cache+0x2b0))})
args.evidence.write_text(json.dumps({'scope':'Original587070 with original440A40 cached synthetic static quad; empty dynamic list; no semantic call hooks.','cases':rows},indent=2)+'\n',encoding='ascii')
print('PASS original587070',len(rows),'height projections;0 semantic hooks')
lines=['// Original587070/440A40 supplied static quad, empty dynamic list, no call hooks.',
 'struct StrutGroundNative {float slope,z;unsigned gap;};',
 'static const StrutGroundNative strut_ground_native[]={']
lines += ['    {'+str(v['slope'])+'F,'+str(v['point'][2])+'F,'+str(v['gap_bits'])+'U},' for v in rows]
lines += ['};','']
args.output.write_text('\n'.join(lines),encoding='ascii')
