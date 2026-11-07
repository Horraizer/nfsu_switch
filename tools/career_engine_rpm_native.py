"""Read-only original PC1.4 USA witness; run from the repository root."""
import argparse
from pathlib import Path
_parser=argparse.ArgumentParser(description=__doc__)
_parser.add_argument('--root',type=Path,required=True)
_parser.add_argument('--out',type=Path,required=True)
_parser.add_argument('--write-fixture',action='store_true')
_options=_parser.parse_args()
report_directory=_options.out.resolve()
_relative=report_directory.relative_to(Path.cwd())
assert _relative.as_posix().isascii() and _relative.parts[0].startswith('build')
assert _options.root.resolve() not in report_directory.parents
report_directory.mkdir(parents=True,exist_ok=False)
def emit_fixture(path,text):
    assert text.isascii()
    if _options.write_fixture:path.write_text(text,encoding='ascii')
    else:assert path.read_text(encoding='ascii')==text,'native fixture is stale'
from pathlib import Path
import sys,hashlib,struct,json
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine,ACTOR,RIDE,CONFIG,STACK
retail=_options.root;raw=(retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);binding=Machine(im);binding.allowed=[(0x57f2eb,0x57f30f)]
binding.put(STACK+0x10,0);binding.run(0x57f2eb,end=0x57f30f,edi=RIDE,esi=CONFIG,ebx=RIDE+8)
sp=binding.reg('esp');args=[binding.get(sp+4*i) for i in range(3)]
assert args==[CONFIG+0x110,RIDE+0x60,RIDE+8] and not binding.events
#575960 is not executed here. Its copy/tuning arithmetic has a separate native
#gate. This slice proves the exact source/destination layout supplied to it.
global_image=(retail/'Global/GLOBALB.BUN').read_bytes();cases=[]
for tag,first,last in chunks(global_image):
 if tag!=0x34600:continue
 first=(first+8+15)&~15
 for at in range(first,last,0xc90):
  record=global_image[at:at+0xc90]
  if len(record)!=0xc90:continue
  name=record[:16].split(b'\0')[0].decode('ascii');maximum=struct.unpack_from('<f',record,0x308)[0]
  if not 1000<maximum<20000:continue
  for fraction in (0,.1,.5,.9,1,1.05):
   rpm=struct.unpack('<f',struct.pack('<f',maximum*fraction))[0]
   m=Machine(im);m.allowed=[(0x526638,0x52665b),(0x523dd7,0x523df7)]
   m.put(ACTOR+0x10,RIDE);m.put(RIDE+0x258,maximum,'f');m.put(RIDE+0x424,0,'B')
   m.run(0x526638,end=0x52665b,ebx=ACTOR,ebp=CONFIG)
   assert m.get(CONFIG+0x1f24,'f')==maximum
   m.put(ACTOR+0x3e0,rpm,'f');m.run(0x523dd7,end=0x523df7,ecx=CONFIG,esi=ACTOR)
   bits=lambda f:struct.unpack('<I',struct.pack('<f',f))[0]
   cases.append(dict(car=name,rpm=bits(rpm),maximum=bits(maximum),output=m.get(CONFIG+0x1f20),hooks=m.events))
assert len(cases)>=100 and all(not c['hooks'] for c in cases)
report=dict(layout_call_arguments=args,layout_hooks=binding.events,cases=cases)
(report_directory/'oracle.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
lines=['// Original526638 constructor read and523DD7 normalized RPM producer.','// Layout:57F2EB passes CarTypeInfo+110 to575960 at RideInfo+60,','// so RideInfo+258 is CarTypeInfo+308 (the tuned maximum RPM).','//575960 is not executed in this witness; see the native tuning gate.','struct NativeEngineRpmWitness {unsigned rpm,maximum,output;};','static constexpr NativeEngineRpmWitness kNativeEngineRpm[]={']
for c in cases:lines.append('    {0x%08XU,0x%08XU,0x%08XU}, //%s'%(c['rpm'],c['maximum'],c['output'],c['car']))
lines.append('};');emit_fixture(root/'tests/career_engine_rpm_native.inc','\n'.join(lines)+'\n')
print('PASS original audio RPM layout/normalization: %d cases; no external hooks'%len(cases))
