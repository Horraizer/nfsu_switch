"""Original007F runtime mass/inertia producer; authenticated retail240SX."""
from pathlib import Path
import sys,json,hashlib
sys.dont_write_bytecode=True
from world_body_trace import Image
from event0_trace import chunks
from physics256_native import Machine
if len(sys.argv)!=3:raise SystemExit('usage: race_inertia_native.py RETAIL OUTPUT')
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
if out==root or root in out.parents:raise ValueError('output must be outside retail root')
out.mkdir(parents=True,exist_ok=False)
exe=(root/'speed.exe').read_bytes();bun=(root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
at=next(at for tag,at,end in chunks(bun) if tag==0x34600)+16+18*0xc90
record=bun[at:at+0xc90];assert record.startswith(b'240SX\0')
m=Machine(Image(exe));m.allowed=[(0x576250,0x57635B)]
src,dst,levels=0x1107000,0x1109000,0x110a000
m.uc.mem_write(src,record);rows=[]
for weight in range(4):
 for item in range(2):
  m.uc.mem_write(dst,record[0x110:]);m.put(levels,weight);m.put(levels+0x2c,item)
  m.run(0x576250,eax=dst,esi=src+0x110,edi=levels)
  rows.append([m.get(dst+offset,'f') for offset in (0x110,0x120,0x134,0x148)])
assert not m.events
report=dict(clip='576250..57635A',precision='007F',record_sha256=hashlib.sha256(record).hexdigest(),ordering='weight0..3; each without/with mass item',rows=rows,limits='Mass/inertia producer only; other tuning and live race policy excluded.')
(out/'report.json').write_text(json.dumps(report,indent=2),encoding='ascii')
lines=['// tools/race_inertia_native.py: original007F, weight0..3, mass item0/1.']
lines += ['{'+','.join(float(v).hex()+'F' for v in row)+'},' for row in rows]
(out/'pins.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
print('PASS original runtime mass/inertia: eight cases, no hooks')
