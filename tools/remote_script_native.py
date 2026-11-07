from pathlib import Path
import sys,struct,json,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import Machine,bits
import argparse
ap=argparse.ArgumentParser(description='Actual remote prerace scripts and RNG state, supplied physical cache')
ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[];proof=[]
for case in range(2):
 m=Machine(im);m.allowed=[(0x400000,0x6a0000),(0x1000800,0x1000807)];m.uc.mem_map(0,0x1000)
 env,status,world,left=0x1100000,0x1103000,0x1104000,0x1105000
 m.put(0x73619c,status);m.put(0x7361f8,world);m.put(0x6f227c,0x12345678+case);m.put(world+0x2d0,0xabcde134+case)
 m.put(status+0xc,1.,'f')
 m.uc.mem_write(0x1000800,b'\xd9\x1d'+struct.pack('<I',left)+b'\xc3')
 for tick in range(1260):
  dt=1/60 if case==0 else 1/30;game=2 if tick<900 else 3 if tick<1080 else 4
  elapsed=(tick-900)*4000//60;rpm=1000.+float(tick%11)*350;load=float((tick*7)%101)
  m.put(status,game);m.put(0x73ad34,elapsed&0xffffffff);m.put(env+0x1f20,rpm,'f');m.put(env+0x1f90,load,'f')
  m.run(0x435f80,eax=status);m.run(0x1000800)
  m.put(env+0x1f1c,rpm,'f');m.put(env+0x1f8c,load,'f')
  m.run(0x524240,(bits(dt),),edi=env)
  rows.append([case,tick,bits(dt),game,m.get(left),bits(rpm),bits(load),m.get(0x6f227c),m.get(env+0x1fac,'B'),m.get(env+0x1fc0),m.get(env+0x1fb4),m.get(env+0x1fb0),m.get(env+0x1fc4),m.get(env+0x1fc8),m.get(env+0x1fcc),m.get(env+0x1fd0,'B'),m.get(env+0x1f1c),m.get(env+0x1f8c),m.get(world+0x2d0)])
 assert not m.events
 proof.append(dict(case=case,addresses=len(m.visited),hooks=[]))
fixture='// Actual524240/435F80 and both original RNGs; supplied caches, no hooks.\nstatic constexpr unsigned kNativeRemoteScript[][19]={\n'
fixture+=''.join('    {'+','.join('0x%08XU'%(n&0xffffffff) for n in row)+'},\n' for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/remote_script_native.inc'
if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
else:assert path.read_text(encoding='ascii')==fixture,'native remote script fixture differs'
print('PASS actual remote rev scripts, original countdown and both RNGs; no hooks')
