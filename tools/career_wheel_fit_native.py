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
import sys,struct,json,hashlib
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,chunks
from event0_trace import table_span
from physics256_native import Machine
retail=_options.root;raw=(retail/'speed.exe').read_bytes();b=(retail/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(b).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
start,end=table_span(b,0x34604,48);af,ae=table_span(b,0x34605,8)
records=[(int((at-start)//48),b[at:at+48]) for at in range(start,end,48) if b[at+16]==17 and b[at+17]]
a,z=next((a,z) for tag,a,z in chunks(b) if tag==0x34600);a=(a+8+15)&~15
m=Machine(Image(raw));m.allowed=[(0x50d5c1,0x50d5e7),(0x50d779,0x50d8b5)]
attrs,part,local=0x10c0000,0x1110000,0x1120000;m.uc.mem_write(attrs,b[af:ae]);m.put(0x6f88a8,1,'B');rows=[]
for at in range(a,z,0xc90):
 if struct.unpack_from('<I',b,at+0xc54)[0]!=0:continue
 name=b[at:at+16].split(b'\0')[0].decode('ascii');outer=struct.unpack_from('<b',b,at+0xdc)[0];matches=[]
 for index,rawrecord in records:
  r=bytearray(rawrecord)
  for off in (20,24):struct.pack_into('<I',r,off,attrs+struct.unpack_from('<I',r,off)[0]*8)
  m.uc.mem_write(part,bytes(r));m.put(local-0x14,part);m.put(local-0x18,outer);m.put(local-0x2c,0)
  m.run(0x50d5c1,end=0x50d5e7,ebp=local)
  m.run(0x50d779,end=0x50d8b5,ebp=local)
  matches.append(bool(m.reg('ecx')&255))
 rows.append(dict(car=name,outer=outer,matches=matches))
assert len(rows)==20 and not m.events
(report_directory/'oracle.json').write_text(json.dumps(dict(indices=[i for i,r in records],rows=rows,hooks=m.events),indent=2)+'\n',encoding='ascii')
lines=['//Original50D5C1 size derivation +50D779 career filter on real wheel attributes.',
 '//CarTypeInfo+DC is supplied by the actual50D4F3 producer. No hooks.',
 'static constexpr unsigned kNativeWheelIndices[]={'+','.join(str(i)+'U' for i,r in records)+'};',
 'struct NativeWheelFit {const char* car;const char* matches;};',
 'static constexpr NativeWheelFit kNativeWheelFits[]={']
for row in rows:lines.append('    {%s,%s},'%(json.dumps(row['car']),json.dumps(''.join('1' if x else '0' for x in row['matches']))))
lines.append('};');emit_fixture(root/'tests/career_wheel_fit_native.inc','\n'.join(lines)+'\n')
print('PASS original career wheel fit: %d cars x%d real records; no hooks'%(len(rows),len(records)))
