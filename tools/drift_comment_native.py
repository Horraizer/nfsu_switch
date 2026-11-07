"""Original drift achievement comment admission/show/hide; no engine output."""
import argparse,hashlib,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
b=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(b).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for delta in [0,1,1999,4000,7999,8000,8001,12000]:
 m=Machine(Image(b));m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
 obj,label=0x1120000,0x1130000;m.put(obj+0x10,1);m.put(obj+0x18,1);m.put(obj+0x88,label);m.put(0x735710,0)
 m.put(0x73ad34,10000);m.run(0x4a7920,(0x12345678,),edi=obj)
 assert m.get(obj+0xad,'B')==1 and m.get(obj+0xb0)==10000 and m.get(label+0x5c)==0x12345678
 m.put(0x73ad34,10000+delta);m.run(0x4a7aa0,ecx=obj)
 rows.append([delta,int((m.get(label+0x1c)&1)==0)])
 assert not m.events
text='// Actual4A7920 and4A7AA0 with enabled drift HUD and absent refresh device.\nstatic constexpr unsigned kNativeDriftComment[][2]={\n'+''.join(' {'+','.join(map(str,r))+'},\n' for r in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/drift_comment_native.inc'
if a.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original drift comment lifetime fixture differs'
print('PASS original drift comment lifetime and label publication, rows='+str(len(rows)))
