"""Actual487B20 magazine setter and487680 matrix on authored pool cameras.
Scalar CPU dispatch and zero camera storage are explicit. No matrix/math hook.
"""
from pathlib import Path
import argparse,struct,sys,hashlib,json
sys.dont_write_bytecode=True
from event0_trace import Image
from world_body_trace import chunks
from hud254_native import Machine
from unicorn.x86_const import UC_X86_REG_EAX
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();assert out.relative_to(Path.cwd()).as_posix().isascii() and a.root.resolve() not in out.parents;out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
im=Image(raw);im.sections=[(s,n,r) for s,n,r,_ in im.sections];m=Machine(im)
m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)));m.write(0x6d4d00,0)
first,last=next((p,q) for t,p,q in chunks(bun) if t==0x30230);counts=struct.unpack_from('<8I',bun,first+8);sizes=[12,16,168,16,20];at=first+40+sum(n*s for n,s in zip(counts,sizes));cam=0x1001000;dest=0x1003000;rows=[]
for index in range(counts[5]):
 values=struct.unpack_from('<11f',bun,at+index*56+12);bits=struct.unpack_from('<11I',bun,at+index*56+12)
 args=[bits[i] for i in (0,1,2,3,4,5,7,6,8,9,10)]
 m.run(0x487b20,args,{UC_X86_REG_EAX:cam});assert m.integer(cam+0x2c)==5
 m.run(0x487680,(dest,),{UC_X86_REG_EAX:cam+0x140})
 rows.append(dict(values=values,matrix=struct.unpack('<16f',m.uc.mem_read(dest,64))))
def literal(value):
 s=format(value,'.9g');return s+('.0F' if '.' not in s and 'e' not in s else 'F')
text='// Actual487B20 and487680, scalar dispatch; no math replacement.\nstruct NativeMagazineCamera {std::array<float,11> values;std::array<float,16> matrix;};\nstatic constexpr NativeMagazineCamera kNativeMagazineCameras[]={\n'
for r in rows:text+='    {{'+','.join(literal(v) for v in r['values'])+'},{'+','.join(literal(v) for v in r['matrix'])+'}},\n'
text+='};\n';target=Path('tests/frontend_magazine_camera_native.inc')
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'magazine camera fixture stale'
(out/'oracle.json').write_text(json.dumps(dict(rows=rows,boundary=__doc__),indent=2)+'\n',encoding='ascii');print('PASS actual magazine camera matrices:',len(rows),'authored cameras')
