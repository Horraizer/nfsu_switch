"""Original PC1.4 USA tournament scores/order for supplied1..8-round results."""
from pathlib import Path
import sys,hashlib,itertools,json,argparse
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine,ACTOR
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
out=a.out.resolve();relative=out.relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');assert a.root.resolve()!=out and a.root.resolve() not in out.parents;out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);perms=list(itertools.permutations(range(1,5)));cases=[]
for count in range(1,9):
 for n in range(24):
  rows=[perms[(n*(2*r+1)+r*7)%24] for r in range(count)]
  m=Machine(im);m.allowed=[(0x5a29c0,0x5a2ba0)]
  m.put(ACTOR+0x5868,count);m.put(ACTOR+0x589c,4)
  for actor in range(4):m.put(ACTOR+0x586c+actor*12+4,0x100+actor)
  for r,row in enumerate(rows):
   race=ACTOR+4+r*0xb0c;m.put(race+0x14,3);m.put(race+0xb08,4)
   for actor,place in enumerate(row):
    m.put(race+0x28+actor*0x15c,place);m.put(race+0x28+actor*0x15c+4,0x100+actor)
  totals=[]
  for actor in range(4):m.run(0x5a29c0,eax=actor,ecx=ACTOR);totals.append(m.reg('eax'))
  order=[]
  for place in range(1,5):m.run(0x5a2ab0,args=(ACTOR,place));order.append(m.reg('eax')-0x100)
  assert not m.events
  if sorted(order)!=list(range(4)):print("unexpected",rows,totals,order);raise SystemExit(1)
  cases.append(dict(rows=rows,total=totals,order=order,instructions=len(m.visited),hooks=[]))
(out/'oracle.json').write_text(json.dumps(cases,indent=2)+'\n',encoding='ascii')
lines=['// Original PC1.4 USA5A29C0 totals and5A2AB0 stable score ordering.','// Supplied four-racer records,192 cases, no external hooks.','struct NativeTournamentWitness {unsigned count;unsigned places[8][4],totals[4],order[4];};','static constexpr NativeTournamentWitness kNativeTournament[]={']
for c in cases:
 rows=list(c['rows'])+[(0,0,0,0)]*(8-len(c['rows']))
 arr=lambda row:'{'+','.join(str(x)+'U' for x in row)+'}'
 lines.append('    {'+str(len(c['rows']))+'U,{'+','.join(map(arr,rows))+'},'+arr(c['total'])+','+arr(c['order'])+'},')
lines.append('};')
text='\n'.join(lines)+'\n';fixture=root/'tests/career_tournament_native.inc'
if a.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'native tournament fixture stale'
print('PASS original tournament scoring/order: %d cases, no hooks'%len(cases))
