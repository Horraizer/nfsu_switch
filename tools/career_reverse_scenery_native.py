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
from world_body_trace import Image
from physics256_native import Machine,CONFIG,ACTOR
raw=(_options.root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
for flags in (0,0x400,0x440,0x410):
 for matrix in ((8192,0,0,0,8192,0,0,0,8192),(6000,5000,-3000,-4000,7000,1000,1234,2468,4000),(-32768,32767,-1,0,-8192,0,8192,2,-2)):
  m=Machine(im);m.allowed=[(0x4462a0,0x4463fd),(0x445580,0x445660),(0x445660,0x445714),(0x5ac69c,0x5ac8d8),(0x674898,0x67490d)]
  #Select the original scalar implementation through its real dispatch slot.
  #This fixes CPU dispatch explicitly; matrix arithmetic is executed, not hooked.
  m.put(0x6d4acc,0x5ac6a2);m.put(CONFIG+0x20,ACTOR);m.put(CONFIG+0x24,1);m.put(ACTOR+0xe,flags,'H')
  m.uc.mem_write(ACTOR+0x1c,struct.pack('<9h',*matrix));m.uc.mem_write(ACTOR+0x10,struct.pack('<3f',12.5,-6.25,123))
  m.run(0x4462a0,(CONFIG,));after=list(struct.unpack('<9h',m.uc.mem_read(ACTOR+0x1c,18)))
  assert not m.events and list(struct.unpack('<3f',m.uc.mem_read(ACTOR+0x10,12)))==[12.5,-6.25,123]
  rows.append(dict(flags=flags,before=matrix,after=after,hooks=m.events))
(report_directory/'oracle.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
lines=['//Original4462A0 with real445580/445660 and scalar5AC6A2 matrix dispatch.',
 '//Only the CPU implementation dispatch slot is supplied; no external hooks.',
 'struct NativeReverseScenery {unsigned flags;std::array<short,9> before,after;};',
 'static constexpr NativeReverseScenery kNativeReverseScenery[]={']
for r in rows:lines.append('    {%dU,{{%s}},{{%s}}},'%(r['flags'],','.join(map(str,r['before'])),','.join(map(str,r['after']))))
lines.append('};');emit_fixture(root/'tests/career_reverse_scenery_native.inc','\n'.join(lines)+'\n')
print('PASS original reverse scenery: %d supplied matrices; scalar CPU dispatch, no hooks'%len(rows))
