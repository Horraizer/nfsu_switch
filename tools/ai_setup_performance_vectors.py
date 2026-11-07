"""Original ordinary-car rating/low-slip from supplied authentic setups."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess
from physics256_native import Machine
from world_body_trace import Image

p=argparse.ArgumentParser()
for key in ('root','out','probe'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();out=a.out.resolve();relative=out.relative_to(Path.cwd())
assert str(relative).isascii() and relative.parts[0].startswith('build')
assert a.root.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
records=[];expected=[]
for index in range(4):
    record=bytearray(bun[1736752:1736752+3216])
    if index in (1,2):struct.pack_into('<f',record,0x110+0x1c0,.5 if index==1 else 0.)
    if index==3:
        for k in range(9):
            at=0x110+0x200+4*k
            struct.pack_into('<f',record,at,struct.unpack_from('<f',record,at)[0]*1.4)
    path=out/('record-'+str(index)+'.bin');path.write_bytes(record);records.append(str(path))
    m=Machine(Image(raw))
    m.allowed=[(0x581b30,0x581d22),(0x46d1c0,0x46d384),(0x583ea0,0x583f20),
               (0x57f61d,0x57f6cb),(0x674898,0x674920),(0x474203,0x47428c),(0x4387e0,0x4387f6)]
    ride,desc,values=0x1100000,0x1101000,0x1102000
    m.uc.mem_write(ride+0x60,bytes(record[0x110:]));m.put(desc,64);m.put(desc+0x10,values)
    m.run(0x581b30,(desc,ride),eax=ride+0x60,budget=3000000)
    m.run(0x57f61d,end=0x57f6cb,edi=ride,esi=desc,budget=1000000)
    owner,engine=0x1103000,0x1104000;m.put(owner+0x24,engine)
    m.run(0x474203,end=0x474276,esi=ride,ebp=owner)
    assert not m.events
    expected.append([m.get(ride+0x428),m.get(engine+0x140)])
env=os.environ.copy()
r=subprocess.run([str(a.probe.resolve()),*records],capture_output=True,text=True,env=env)
(out/'portable.log').write_text(r.stdout+r.stderr,encoding='ascii')
actual=[list(map(int,row.split())) for row in r.stdout.splitlines()]
okay=r.returncode==0 and actual==expected
(out/'comparison.json').write_text(json.dumps(dict(expected=expected,actual=actual,passed=okay,
    boundary='Original581B30/57F61D/474203, supplied valid stock and directed modified setup. Not adaptive event upgrades or live trajectory.'),indent=2)+'\n',encoding='ascii')
print('PASS' if okay else 'FAIL',len(expected),'rating/low-slip comparisons')
raise SystemExit(0 if okay else 1)
