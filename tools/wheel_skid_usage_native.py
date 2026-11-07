"""Native587830 normalized wheel+168 output used by skid emission.
Bounded stock240SX owner setup; no world, rendering or whole-race claim.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from hud254_trace import PE
from physics256_native import Machine,bits
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');a.out.mkdir(exist_ok=False,parents=True)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
m=Machine(PE(raw));m.allowed=[(0x5868e0,0x586960),(0x587830,0x5879ae)]
setup,owner,wheel=0x1100000,0x1101000,0x1102000
m.uc.mem_write(setup,bun[1736752+0x110:1736752+3216]);m.put(owner+0x3c0,0)
m.run(0x5868e0,(setup+0x10,setup+0xd0,setup+0x270),eax=wheel,ecx=owner)
initial=bytes(m.uc.mem_read(wheel,0x200));rows=[]
for fx,fy,load,torque,sliding,locked,contact in [(0,0,3,0,0,0,1),(.4,.7,3,0,0,0,1),(10,2,3,0,0,0,1),(.1,.2,3,0,1,0,1),(.1,.2,3,0,0,1,1),(1,1,3,0,0,0,0),(1,1,0,0,0,0,1),(1,.5,3,2,0,0,1),(0,0,3,0,1,0,0),(9,2,10,0,0,0,1)]:
 m.uc.mem_write(wheel,initial)
 for offset,value in [(0x118,fx),(0x11c,fy),(0x158,load),(0x108,torque),(0x178,1),(0x17c,1),(0x180,1)]:m.put(wheel+offset,float(value),'f')
 m.put(wheel+0x150,1);m.put(wheel+0x10c,locked);m.put(wheel+0x154,sliding,'B');m.put(wheel+0x155,contact,'B')
 m.run(0x587830,(0,),end=0x5879ad,eax=wheel)
 rows.append([bits(float(v)) for v in (fx,fy,load,torque)]+[sliding,locked,contact,m.get(wheel+0x168)])
assert not m.events
fixture='// Original587830 through5879AD, stock240SX setup, no call hooks.\nstatic constexpr unsigned kNativeWheelSkidUsage[][8]={\n'+'\n'.join('    {'+','.join(str(v)+'U' for v in row)+'},' for row in rows)+'\n};\n'
path=root/'tests/wheel_skid_usage_native.inc'
if a.write_fixture:path.write_text(fixture,encoding='ascii')
else:assert path.read_text(encoding='ascii')==fixture
(a.out/'evidence.json').write_text(json.dumps({'cases':rows,'hooks':m.events,'scope':__doc__,'exe_sha256':hashlib.sha256(raw).hexdigest()},indent=2)+'\n',encoding='ascii')
print('PASS original normalized wheel friction usage:',len(rows),'cases, no hooks')
