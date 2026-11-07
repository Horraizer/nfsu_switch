"""Execute original garage target-speed setters; use existing native road update separately."""
from pathlib import Path
import argparse,sys,json,hashlib,struct
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');a.out.mkdir(exist_ok=False,parents=True)
raw=(a.root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x505c29,0x505c3f),(0x50cd34,0x50cd3e)];rows=[]
for group,start,end in [(0,0x505c29,0x505c3f),(1,0x50cd34,0x50cd3e)]:
 m.put(0x746de0,0);m.run(start,end=end);rows.append([group,m.get(0x746de0)])
assert not m.events
fixture='// Original505C29/39 generic and50CD34/39 rim target setters, no hooks.\nstatic constexpr unsigned kNativeGarageMotion[][2]={\n'+'\n'.join('    {'+','.join(str(v)+'U' for v in row)+'},' for row in rows)+'\n};\n'
path=root/'tests/frontend_garage_motion_native.inc'
if a.write_fixture:path.write_text(fixture,encoding='ascii')
else:assert path.read_text(encoding='ascii')==fixture
(a.out/'evidence.json').write_text(json.dumps({'rows':rows,'hooks':m.events,'boundary':'Target setter slices only; initialization and frontend scheduler supplied'},indent=2)+'\n',encoding='ascii')
print('PASS original generic/rim garage target speeds, no hooks')
