from pathlib import Path
import sys,json,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import Machine,bits
import argparse
ap=argparse.ArgumentParser(description='Actual536800 and52C1D0 supplied human scalar state, no hooks')
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
root=args.retail_root
raw=(root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
env,actor,owner,status,parent,settings=0x1100000,0x1103000,0x1104000,0x1105000,0x1106000,0x1110000
m.put(env+0x234,actor);m.put(actor+0x18,owner);m.put(0x73619c,status);m.put(0x7356a8,parent);m.put(0x7356ac,settings)
m.run(0x5364e0,(settings,));m.run(0x5368b0,(settings,3,0xbf800000,0),edx=0)
rows=[];changes=[];previous_nos=False
for tick in range(2600):
 time=tick/60;speed=0 if tick<400 or tick>=2200 else 80.;on=7. if tick>=420 else 0.;off=0. if tick<2200 else time
 nos=660<=tick<800;crash=6 if 1050<=tick<1120 else 1
 m.put(env+0x240,time,'f');m.put(env+0x1f18,on,'f');m.put(env+0x1f14,off,'f');m.put(env+0x1d10,7.,'f')
 m.put(env+0x1f1c,7000.,'f');m.put(env+0x1d18,6);m.put(actor+0x3d4,speed,'f');m.put(owner+0x60,crash);m.put(status+0x14,tick*4000//60)
 m.run(0x536800,(settings,bits(1/60)))
 before=[m.get(settings+c*0x130+8) for c in (1,9,10)]+[m.get(settings+0x35cc)]
 if nos!=previous_nos:
  if nos:m.run(0x5368b0,(settings,5,0xbf800000,0),edx=0)
  else:m.run(0x536a70,(settings,5,0))
 previous_nos=nos
 m.run(0x52c1d0,esi=env)
 after=[m.get(settings+c*0x130+8) for c in (1,9,10)]+[m.get(settings+0x35cc)]
 if before!=after:changes.append([tick,before,after])
 rows.append([tick,bits(time),bits(speed),bits(on),bits(off),bits(7.),bits(7000.),6,crash,int(nos)]+before+[m.get(settings+0x3474)])
assert not m.events
fixture='// Actual536800 then52C1D0; supplied scalar state, no hooks.\nstatic constexpr unsigned kNativeHumanProfiles[][15]={\n'
fixture+=''.join('    {'+','.join('0x%08XU'%(n&0xffffffff) for n in row)+'},\n' for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/human_profiles_native.inc'
if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
else:assert path.read_text(encoding='ascii')==fixture,'native human profile fixture differs'
assert not changes
print('PASS original profile predicates and next-frame publication; profiles',sorted(set(r[-1] for r in rows)))
