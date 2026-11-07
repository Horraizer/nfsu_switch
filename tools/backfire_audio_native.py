"""Original527480 consecutive backfire decisions with declared output observer.

526A40 option gate, timer, curves, original sound RNG and ftol all execute.
5272F0 is observed as the output boundary; it supplies no decision result.
"""
import argparse,hashlib,sys,struct
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
b=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for config,(level,dt,seed)in enumerate([(0,1/60,0x12345678),(1,1/30,0x76543210),(2,1/60,0x12345678),(3,1/30,0x76543210)]):
 m=Machine(Image(b));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
 owner=0x1120000;m.put(owner,0x6beeb0);m.put(owner+0x1efc,level);m.put(owner+0x1148,1,'B');m.put(owner+0x11d0,max(0,level-2));m.put(owner+0x1158,1,'B');m.put(0x6f227c,seed);m.put(0x78eae0,0)
 observed=[]
 def output(x):
  sp=x.reg('esp');assert x.get(sp+4)==owner+0x113c
  observed.append([x.get(sp+8),x.get(sp+12)]);x.ret(20,value=0)
 m.hook(0x5272f0,'backfire-output-observer',callback=output)
 for tick in range(480):
  rpm=[7500.,2800.,2000.,2000.01,4500.,9000.][min(tick//80,5)]
  on=100<=tick<140 or 290<=tick<310
  m.put(owner+0x1f10,int(on),'B');m.put(owner+0x1f1c,rpm,'f')
  prior=m.get(0x6f227c);counter=m.get(0x78eae0);observed.clear();m.run(0x527480,(owner,bits(dt)))
  assert len(observed)<=1
  rows.append([config,level,bits(dt),bits(rpm),int(on),prior,counter,m.get(owner+0x1134),m.get(owner+0x1130),m.get(owner+0x1138),int(bool(observed)),*(observed[0]if observed else [0,0]),m.get(0x6f227c),m.get(0x78eae0)])
 assert all(e['hook']=='backfire-output-observer'for e in m.events)
#521C80 row1 selects CAR_1 independently of engine sound group.
for level in [0,1,2,3]:
 m=Machine(Image(b));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
 manager=0x1120000;m.put(manager+0x38,8);m.put(manager+0x3c,1);m.put(manager+0x40,level)
 m.run(0x521c80,end=0x521e0e,eax=manager)
 def string(address):return bytes(m.uc.mem_read(address,128)).split(b'\0')[0].decode('ascii')
 assert string(m.get(manager+0x30))=='Car_1'+chr(92)+'Eng_UG_'+str(level)+chr(92)
 assert string(m.get(manager+0x2c))=='SparkChatter_'+('01'if level>=2 else '00')
 assert not m.events
text='// Actual527480,526A40,5326F0,532840,564A50/80 and674898; output-only hook.\nstatic constexpr unsigned kNativeBackfireAudio[][15]={\n'+''.join(' {'+','.join('0x%08XU'%v for v in r)+'},\n'for r in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/backfire_audio_native.inc'
if a.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original backfire fixture differs'
print('PASS original backfire consecutive controls, rows='+str(len(rows))+' events='+str(sum(r[10]for r in rows)))
