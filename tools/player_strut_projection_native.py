"""Original587070 with cached supplied static planes, tilted body up, empty object list."""
from pathlib import Path
import argparse,hashlib,json,struct
from hud254_trace import PE
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--evidence',type=Path,required=True)
a=ap.parse_args()
for f in (a.output,a.evidence):
 assert not f.exists()
 assert f.resolve()!=a.root.resolve() and a.root.resolve() not in f.resolve().parents
b=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(b).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for sx,sy in [(0.,0.),(.25,0.),(-.25,0.),(.25,.5),(-.25,-.5),(.5,-.25),(.137,.271),(-.213,.463)]:
 for up in [(0.,0.,1.),(.3,.4,.8660254),(.6,0.,.8),(0.,.6,.8),(1.,0.,0.),(.253,.143,.957)]:
  for z in [-1.,-.25,.25,1.25,4.]:
   m=Machine(PE(b));m.allowed=[(0x587070,0x58716a),(0x440a40,0x440cdc),(0x4400e0,0x4401f6),(0x577060,0x577250),(0x40cb70,0x40cb7b),(0x441d80,0x441ecc),(0x4387e0,0x4387f6),(0x674898,0x67490d),(0x564b10,0x564c00),(0x564cd0,0x564f20),(0x565230,0x565300),(0x5abd00,0x5abf00),(0x583ea0,0x583f18),(0x567370,0x5673dc),(0x1101500,0x1101507)]
   strut,owner,cache,poly,point,normal,material,body,result,tramp=0x1100000,0x1100200,0x1100400,0x1100800,0x1101000,0x1101100,0x1101200,0x1101300,0x1101400,0x1101500
   m.put(strut+0x20,owner);m.put(owner+4,cache);m.put(owner+8,owner+0x10);m.put(0x73b14c,0x73b14c);m.put(0x6d4ac4,0x5abdb5)
   m.uc.mem_write(cache,struct.pack('<4f',-100,-100,100,100));m.put(cache+0x2a4,1);m.put(cache+0x20,poly)
   m.put(poly+9,4,'B');m.put(poly+11,0,'B');m.uc.mem_write(poly+0x14,struct.pack('<4f',-5,-5,5,5))
   for k,(x,y) in enumerate([(-5,-5),(5,-5),(5,5),(-5,5)]):m.uc.mem_write(poly+0x30+16*k,struct.pack('<3f',x,y,sx*x+sy*y+.3))
   m.uc.mem_write(normal,struct.pack('<3f',-sx*100.,-sy*100.,100.));m.run(0x567370,ecx=poly+0x70,edx=normal)
   m.put(poly+0x7c,m.get(poly+0x78,'f')*.3,'f')
   hub=[1.371,.531,z];m.uc.mem_write(point,struct.pack('<3f',*hub));m.uc.mem_write(body+0x20,struct.pack('<3f',*up))
   m.uc.mem_write(tramp,b'\xd9\x1d'+struct.pack('<I',result)+b'\xc3')
   m.run(0x587070,(strut,material,body),edi=point,esi=normal);m.run(tramp)
   assert not m.events
   rows.append([bits(v) for v in hub+list(up)]+[m.get(poly+0x70+4*i) for i in range(4)]+[m.get(strut+0x15c),m.get(result)])
lines=['// Original587070/440A40 supplied planes and tilted up; empty dynamic list.',
 '// hubXYZ, bodyUpXYZ, selected planeXYZD, bump, projected gap. CW007F; no hooks.',
 'static const std::uint32_t strut_projection_native[][12]={']
lines+=['    {'+','.join('0x%08XU'%v for v in row)+'},' for row in rows];lines+=['};','']
a.output.write_text('\n'.join(lines),encoding='ascii')
a.evidence.write_text(json.dumps(dict(executable_sha256=sha,cases=len(rows),hooks=[],scope=__doc__),indent=2)+'\n',encoding='ascii')
assert hashlib.sha256((a.root/'speed.exe').read_bytes()).hexdigest()==sha
print('PASS original tilted strut projection',len(rows),'cases; zero semantic hooks; retail unchanged')
