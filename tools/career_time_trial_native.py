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
import sys,hashlib,json
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine,CONFIG
from hud254_native import lap_native
raw=(_options.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);cases=[];builders=[]
for difficulty in range(3):
 m=Machine(im);m.allowed=[(0x4b5fdf,0x4b6006),(0x674898,0x67490d)]
 for i,seconds in enumerate((115,105,95)):m.put(CONFIG+0x58bc+4*i,seconds,'f')
 m.run(0x4b5fdf,end=0x4b6006,ebx=CONFIG,ecx=difficulty)
 assert m.get(0x78a30c)==2 and not m.events
 builders.append(dict(difficulty=difficulty,mode=m.get(0x78a30c),limit=m.get(0x78a310),hooks=m.events))
for limit in (460000,420000,380000,688000,648000,608000):
 for elapsed in (1,limit-67,limit-1,limit,limit+1,limit+67):
  m=Machine(im);m.allowed=[(0x5a3098,0x5a30a6)]
  m.put(0x78a310,limit);m.run(0x5a3098,end=0x5a30a6,edx=elapsed)
  success=bool(m.reg('eax')&255);assert success==(elapsed<limit) and not m.events
  hud=lap_native(im,now=4000+elapsed,start=4000,time_limit=limit)
  calls=[c for c in hud['calls'] if c.get('target')=='current']
  assert len(calls)==1 and 'text' in calls[0]
  cases.append(dict(limit=limit,elapsed=elapsed,success=success,text=calls[0]['text'],hooks=m.events))
(report_directory/'oracle.json').write_text(json.dumps(dict(builders=builders,cases=cases),indent=2)+'\n',encoding='ascii')
lines=['//Original4B5FDF type4 -> runtime mode2 +78A310;5A3098 strict reward check.',
 '//HUD4A1932 countdown executes the original formatter; FEng calls are captured.',
 '//This is NOT435875: byte78A346 selects a different challenge mode.',
 'struct NativeTimeTrialWitness {int limit,elapsed;bool success;const char* text;};',
 'static constexpr NativeTimeTrialWitness kNativeTimeTrial[]={']
for c in cases:lines.append('    {%d,%d,%s,%s},'%(c['limit'],c['elapsed'],'true' if c['success'] else 'false',json.dumps(c['text'])))
lines.append('};');emit_fixture(root/'tests/career_time_trial_native.inc','\n'.join(lines)+'\n')
print('PASS native career time-trial mode binding, strict reward and countdown: %d cases'%len(cases))
