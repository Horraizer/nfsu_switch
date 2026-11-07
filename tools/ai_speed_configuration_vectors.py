"""Check portable selected fields against original462070 initialized memory."""
import argparse, hashlib, json, subprocess
from pathlib import Path
from world_body_trace import Image
from physics256_native import Machine

p=argparse.ArgumentParser()
for key in ('root','out','probe'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();relative=a.out.resolve().relative_to(Path.cwd())
assert str(relative).isascii() and relative.parts[0].startswith('build')
a.out.mkdir(exist_ok=False,parents=True)
exe=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
m=Machine(Image(exe));m.allowed=[(0x462070,0x462098)]
m.uc.mem_write(0x1100000,bun[2181120:2181120+8+0x82c]);m.run(0x462070,(0x1100000,))
assert m.reg('eax')==1 and not m.events
payload=a.out/'payload.bin';payload.write_bytes(bun[2181128:2181128+0x82c])
run=subprocess.run([str(a.probe.resolve()),str(payload.resolve())],capture_output=True,text=True)
(a.out/'portable.log').write_text(run.stdout+run.stderr,encoding='ascii');run.check_returncode()
lines=run.stdout.splitlines();assert len(lines)==11
rows=[]
for context,line in zip(range(-1,10),lines):
    at=0x779ff8+max(0,context)*0x54
    expected=[m.get(at+4*j) for j in range(3)]+[m.get(at+0x14),m.get(0x77a498),m.get(at+0x50,'B')]
    fields=line.split();assert fields[:2]==['CONFIG',str(context)]
    actual=list(map(int,fields[2:]));assert actual==expected,(context,expected,actual)
    rows.append(dict(context=context,expected=expected,actual=actual))
(a.out/'comparison.json').write_text(json.dumps(dict(cases=rows,hooks=m.events,scope=__doc__),indent=2)+'\n',encoding='ascii')
print('PASS: 11 selected-context decodes match original initialized table bits; no semantic hooks')
