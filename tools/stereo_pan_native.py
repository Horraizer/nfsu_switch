"""Actual original stereo panning table and software gain publication.

652FD0,6533C0,64D280,64D1E0,6513B0,6512F0 and656870 execute unchanged.
Only lock and table-allocation IO are supplied. Stereo90/270, master1 and
one admitted mono software voice are explicit inputs. Default normal gain
comes from real64FBD0. This does not certify DirectSound/HRTF or PCM mixing.
"""
import argparse,hashlib,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');args=p.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
heap,voice,wave,output,params=0x1120000,0x1140000,0x1160000,0x1180000,0x1190000;allocations=[]
def allocate(x):
 size=x.get(x.reg('esp')+4);allocations.append(size);assert size==512;x.ret(value=heap)
m.hook(0x64e520,'device-lock');m.hook(0x64e540,'device-unlock');m.hook(0x650fd0,'table-memory-allocation',callback=allocate)
m.put(0x713db7,2,'B');m.put(0x713cdc,2,'B');m.put(0x713ce0,1)
m.put(0x713cc8,90.,'f');m.put(0x713ccc,270.,'f');m.put(0x6e9bc8,1.,'f')
m.run(0x64fbd0,(params,));assert m.get(params+4,'B')==127
m.run(0x652fd0,budget=1000000);assert allocations==[512]
assert m.get(0x714034)==heap and m.get(0x714038)==heap+256
m.put(0x713f48,voice);m.put(0x71400c,wave);m.put(0x71666c,output)
m.put(voice+4,0,'h');m.put(voice+0x23,1,'B');m.put(voice+0x24,4,'B');m.put(voice+0x5f,m.get(params+4,'B'),'B')
rows=[]
angles=sorted(set([i*257 for i in range(256)]+[0,1,255,256,16383,16384,16385,32767,32768,32769,49151,49152,49153,65534,65535]))
for angle in angles:
 for gain in [0,1,2048,10000,18000,32767,32768]:
  m.run(0x64d280,(0,angle));m.run(0x64d1e0,(0,gain))
  rows.append([angle,gain,m.get(output+0x1c),m.get(output+0x20)])
assert len(m.events)==3 and [v['hook']for v in m.events]==['device-lock','table-memory-allocation','device-unlock']
text='// Actual original stereo table and software channel gain consumers; no math hooks.\nstatic constexpr unsigned kNativeStereoPan[][4]={\n'+''.join(' {'+','.join('0x%08XU'%v for v in row)+'},\n'for row in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/stereo_pan_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original stereo gain fixture differs'
print('PASS actual stereo panning/gain publication, rows='+str(len(rows)))
