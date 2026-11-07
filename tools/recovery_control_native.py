"""Original recovery timer/ring, hold choice and progress-rate kernel.

462430 stops immediately before the planner call. Fractional race speed
is a supplied actor-record input.462AE0 receives a declared outside-road
distance through a tiny fld/ret input stub.433DB4 receives supplied x87
current/previous full progress. No native algorithm/result is hooked.
Retail files/instructions stay unchanged; fixtures are numeric only.
"""
from pathlib import Path
import sys,argparse,hashlib,struct,itertools
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);histories=[];holds=[];rates=[]
for trace,dt in enumerate((1/60,67/4000,.25,0.)):
 m=Machine(image);controller,actor,record,history,buffer,physics=range(0x1160000,0x1166000,0x1000)
 m.allowed=[(0x462430,0x4624c3),(0x586640,0x58668c),(0x584270,0x5842c4),(0x674898,0x67490e)]
 m.put(controller+4,actor);m.put(actor+0x28,1);m.put(actor+0x34,physics);m.put(actor+0x1c,record)
 m.put(controller+0x4d8,6000);m.put(controller+0x4dc,history)
 m.put(history+4,4,'B');m.put(history+5,5,'B');m.put(history+0x10,buffer);m.put(0x7372b0,2000.,'f')
 for frame in range(720):
  rate=(0.,.0001,.002,-.0001)[(frame//120)%4];m.put(record+0x18,rate,'f')
  m.run(0x462430,(bits(dt),),end=0x4624b5,ecx=controller)
  if(frame+1)%30==0:
   histories.append([trace,30,bits(dt),bits(rate),bits(2000.),m.get(history+6,'B'),m.get(history+7,'B'),m.get(controller+0x4d8),m.get(history+8),m.get(history+12),*[m.get(buffer+4*j)for j in range(5)]])
 assert not m.events
for outside,count,average in itertools.product((-1.,0.,.001,1.,50.),(0,4,5),(-.1,0.,.199999,.2,1.)):
 m=Machine(image);controller,history,vtable=0x1160000,0x1161000,0x1162000
 stub,store,value,output=0x1104000,0x1104100,0x1104200,0x1104300
 m.allowed=[(0x462ae0,0x462b30),(stub,stub+7),(store,store+7)]
 m.put(controller,vtable);m.put(vtable+0x28,stub);m.put(controller+0x4dc,history)
 m.put(history+5,5,'B');m.put(history+6,count,'B');m.put(history+12,average,'f');m.put(value,outside,'f')
 m.uc.mem_write(stub,b'\xd9\x05'+struct.pack('<I',value)+b'\xc3')
 m.uc.mem_write(store,b'\xd9\x1d'+struct.pack('<I',output)+b'\xc3')
 m.run(0x462ae0,ecx=controller);m.run(store);assert not m.events
 holds.append([bits(outside),count,bits(average),m.get(output)])
for now,previous,dt,prior in itertools.product((0.,.001,1.,3.),(0.,.25,1.),(0.,1/60,67/4000,.5),(-.2,0.,.2)):
 m=Machine(image);record,stub,values=0x1160000,0x1104000,0x1104200
 m.allowed=[(0x433db4,0x433def),(stub,stub+13)]
 m.put(record+0x18,prior,'f');m.put(0x73457c,dt,'f');m.put(values,previous,'f');m.put(values+4,now,'f')
 m.uc.mem_write(stub,b'\xd9\x05'+struct.pack('<I',values)+b'\xd9\x05'+struct.pack('<I',values+4)+b'\xc3')
 m.run(stub);m.run(0x433db4,end=0x433def,edi=record);assert not m.events
 rates.append([bits(now),bits(previous),bits(dt),bits(prior),m.get(record+0x18)])
text='// Original462430/586640/584270,462AE0 and433DB4; declared IO only.\n'
for name,rows,width in(('History',histories,15),('Hold',holds,4),('Rate',rates,5)):
 text+='static constexpr unsigned kNativeRecovery'+name+'[]['+str(width)+']={\n'
 text+=''.join('{'+','.join(str(v)+'U'for v in row)+'},\n'for row in rows)+'};\n'
p=Path(__file__).resolve().parents[1]/'tests/recovery_control_native.inc'
if a.write_fixture:p.write_bytes(text.encode('ascii'))
else:assert p.read_bytes()==text.encode('ascii'),'native recovery control fixture differs'
print('PASS original recovery history/hold/rate, '+str(len(histories)+len(holds)+len(rates))+' observations')
