"""Read-only original stock UI ratings; supplied retail setups, not gameplay.
Unicorn uses initialized original globals and fresh per-function trampolines.
"""
from pathlib import Path
import sys,struct,json,hashlib,argparse,subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--probe',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
a=parser.parse_args()
relative=a.out.resolve().relative_to(Path.cwd())
assert str(relative).isascii() and relative.parts[0].startswith('build')
assert a.root.resolve() not in a.out.resolve().parents
a.out.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine,STOP
root=a.root
raw=(root/'speed.exe').read_bytes();bun=(root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
m=Machine(Image(raw));m.allowed=[(0x581d30,0x5824ac),(0x581b30,0x581d22),(0x46d1c0,0x46d384),(0x583ea0,0x583f20),(0x674898,0x674920),(0x4387e0,0x4387f6),(0x5801b0,0x58058c)]
records=[bun[1678864+i*3216:1678864+(i+1)*3216] for i in range(35)]
for record in records:
 if struct.unpack_from('<I',record,0xc54)[0]==0:
  m.uc.mem_write(0x1100000,record[0x110:]);m.run(0x581d30,ecx=0x1100000)
bounds=[m.get(0x6f0d30+i*4,'f') for i in range(10)]

expected={}
for record in records:
 if record.split(b"\0")[0] not in (b"GOLF",b"MIATA",b"NEON",b"PEUGOT",b"CIVIC"):continue
 name=record.split(b'\0')[0].decode('ascii');ride=0x1102000;values=0x1103000
 m.uc.mem_write(ride,bytes(0x800));m.uc.mem_write(ride+0x60,record[0x110:0x110+0x3b0])
 m.put(ride+0x410,64);m.put(ride+0x420,values)
 m.run(0x581b30,(ride+0x410,ride),eax=ride+0x60,budget=3000000)
 result=[]
 for entry in (0x5802c0,0x5801b0,0x580400):
  thunk=STOP+0x1000+64*len(result);dest=STOP+0x1100
  code=b'\xe8'+struct.pack('<i',entry-thunk-5)+b'\xd9\x1d'+struct.pack('<I',dest)+b'\xc3'
  m.uc.mem_write(thunk,code);m.allowed.append((thunk,thunk+len(code)))
  m.run(thunk,eax=ride,esi=ride,budget=1000000);result.append(m.get(dest))
 print(name,result);expected[name]=result
assert not m.events
(a.out/'native.json').write_text(json.dumps(dict(bounds=bounds,ratings=expected,boundary='Original retail initialized bounds and stock setups; FPCW007F; no live gameplay or tuned-car claim.'),indent=2)+'\n',encoding='ascii')

r=subprocess.run([str(a.probe.resolve()),str(root)],capture_output=True,text=True)
(a.out/'portable.log').write_text(r.stdout+r.stderr,encoding='ascii')
actual={}
for line in r.stdout.splitlines():
 if line.startswith('RATING '):
  cells=line.split();actual[cells[1]]=list(map(int,cells[2:]))
okay=r.returncode==0 and 'PASS career retail' in r.stdout and expected==actual and len(expected)==5
(a.out/'comparison.json').write_text(json.dumps(dict(passed=okay,original=expected,portable=actual),indent=2)+'\n',encoding='ascii')
print('PASS' if okay else 'FAIL','15 original stock frontend ratings')
raise SystemExit(0 if okay else 1)
