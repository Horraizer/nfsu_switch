"""Read-only original retirement/visibility consumers on supplied view state."""
import argparse, hashlib, json, sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine, ACTOR, BODY, CONFIG, bits
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
a=parser.parse_args();out=a.out.resolve();rel=out.relative_to(Path.cwd())
assert rel.as_posix().isascii() and rel.parts[0].startswith('build')
assert a.root.resolve()!=out and a.root.resolve() not in out.parents
out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
for ghost in (0,1):
 for since in (0,0x7fffffff,10000):
  for elapsed in (0,1,7999,8000,8001,20000):
   for dt in (0.,.001,.00101,1./60.,.4/60.):
    for frame in (2,3):
     m=Machine(im);m.allowed=[(0x435344,0x435364),(0x570b90,0x570c11),(0x4280a0,0x4280f0),(0x466890,0x4668e3)]
     now=10000+elapsed
     m.put(0x73ad34,now);m.put(0x7361f0,CONFIG);m.put(0x73457c,dt,'f');m.put(0x7364dc,frame)
     m.put(BODY+0x140,ACTOR)
     m.run(0x435344,end=0x435364,esi=ACTOR,ecx=since)
     assert m.get(ACTOR+0x884)==since and m.get(ACTOR+0x888,'f')==2. and m.get(ACTOR+0x8a8,'B')==1 and m.get(ACTOR+0x8a9,'B')==0
     m.put(ACTOR+0x8a8,ghost,'B')
     m.run(0x570b90,(CONFIG+0x100,),edi=ACTOR)
     hidden=m.reg('eax')
     m.run(0x466890,ecx=BODY)
     assert not m.events
     rows.append(dict(ghost=ghost,since=since,now=now,dt=bits(dt),frame=frame,hidden=hidden,body_ghost=m.reg('eax')))
#57A300 is the shared ordinary-neighbor eligibility virtual. The actor remains
#allocated and published; only its permanent ghost flag removes the candidate.
neighbor_rows=[]
for ghost in (0,1):
 for active in (0,1):
  m=Machine(im);m.allowed=[(0x57a300,0x57a324)]
  m.put(CONFIG+0x90,ACTOR);m.put(ACTOR+0x8a8,ghost,'B');m.put(ACTOR+0x4d0,active,'B')
  m.run(0x57a300,ecx=CONFIG)
  admitted=m.reg('eax');assert admitted==int(not ghost and active) and not m.events
  neighbor_rows.append([ghost,active,admitted])
(out/'neighbor.json').write_text(json.dumps(neighbor_rows,indent=2)+'\n',encoding='ascii')
(out/'oracle.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
lines=['// Original435344 transition;570B90/4280A0 visibility and466890 ghost consumers.',
 '// Supplied ordinary single-player view, no replay override or per-view mask.',
 '// Authenticated instructions only; no external hooks.',
 'struct NativeActorRetirement {bool ghost;int since,now;unsigned dt,frame;bool hidden,body_ghost;};',
 'static constexpr NativeActorRetirement kNativeActorRetirement[]={']
for r in rows:lines.append('    {%s,%d,%d,0x%08xU,%d,%s,%s},'%('true' if r['ghost'] else 'false',r['since'],r['now'],r['dt'],r['frame'],'true' if r['hidden'] else 'false','true' if r['body_ghost'] else 'false'))
lines.append('};');text='\n'.join(lines)+'\n';target=Path('tests/race_actor_retirement_native.inc')
assert text.isascii()
if a.write_fixture:target.write_text(text,encoding='ascii')
else:assert target.read_text(encoding='ascii')==text,'native retirement fixture stale'
print('PASS original actor retirement consumers; supplied view state; no hooks')
