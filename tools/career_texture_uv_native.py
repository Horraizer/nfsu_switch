"""Read-only PC1.4 USA world-texture UV transform witness; run from repository root."""
import argparse
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
options=parser.parse_args()
out=options.out.resolve();relative=out.relative_to(Path.cwd())
assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
assert options.root.resolve() not in out.parents and options.root.resolve()!=out
out.mkdir(parents=True,exist_ok=False)
import sys,hashlib,json,struct
from pathlib import Path
sys.dont_write_bytecode=True
root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine,CONFIG,STACK
raw=(options.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
for mode,period,u,v in ((0,256,123,-456),(1,256,0,30720),(1,256,16384,-24576),(2,384,-32768,32767),(3,256,8192,-16384)):
 for ticks in (0,1,67,2000,4000,6000,1234567,-2000,16777217):
  m=Machine(im);m.allowed=[(0x40f599,0x40f627),(0x40f681,0x40f685),(0x40ec20,0x40ec8a),(0x40ec90,0x40ecfa),(0x674898,0x67490d)]
  m.put(CONFIG+0x52,mode,'B');m.put(CONFIG+0x58,period,'h');m.put(CONFIG+0x5a,u,'h');m.put(CONFIG+0x5c,v,'h');m.put(CONFIG+0x5e,u,'h');m.put(CONFIG+0x60,v,'h');m.put(0x73ad34,ticks,'i');m.put(0x7363e0,0,'f');m.put(0x7363e4,0,'f')
  m.run(0x40f599,end=0x40f685,ebx=CONFIG)
  assert not m.events
  rows.append(dict(mode=mode,period=period,u=u,v=v,ticks=ticks,result=[m.get(STACK+0x20,'f'),m.get(STACK+0x24,'f')],hooks=m.events))
p=out/'oracle.json';p.write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
print('PASS native texture UV binding: %d cases, no hooks'%len(rows))

lines=['//Original40F599 binding +40EC20/40EC90 +674898, no hooks.',
       'struct NativeTextureUv {int mode,period,u,v,ticks;float result[2];};',
       'static constexpr NativeTextureUv kNativeTextureUv[]={']
for r in rows:lines.append('    {%d,%d,%d,%d,%d,{%s,%s}},'%(r['mode'],r['period'],r['u'],r['v'],r['ticks'],repr(r['result'][0])+'F',repr(r['result'][1])+'F'))
lines.append('};');text='\n'.join(lines)+'\n';assert text.isascii()
fixture=root/'tests/career_texture_uv_native.inc'
if options.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'native fixture is stale'
