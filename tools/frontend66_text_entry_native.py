"""Original nonloop text Init/FadeIn arithmetic at supplied phase boundaries."""
from pathlib import Path
import argparse,hashlib,struct,sys
sys.dont_write_bytecode=True
from event0_trace import Image,jdlz
from hud254_native import Machine
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
root=parser.parse_args().retail_root
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
front=jdlz((root/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
o=0xbec98c;f=jdlz(front[o:o+struct.unpack_from('<I',front,o+12)[0]])
wanted={('main_menu_title','Init'):{0:0,1100:0,1110:80,1120:160,1400:160},
        ('Description','Init'):{0:0,1200:0},
        ('Description','FadeIn'):{0:0,120:128,240:255},
        ('Options','Init'):{0:0,1320:0,1360:128,1400:255}}
seen=set();p=0
while p+16<len(f):
 if f[p:p+4]!=b'FOb\xea':p+=1;continue
 outer=struct.unpack_from('<I',f,p+4)[0];end=p+16+struct.unpack_from('<I',f,p+12)[0]
 assert end<=p+8+outer<=len(f)
 q=p+16;fields={}
 while q<end:
  size=struct.unpack_from('<H',f,q+2)[0];assert q+4+size<=end
  fields[f[q:q+2]]=f[q+4:q+4+size];q+=4+size
 name=fields.get(b'On',b'').rstrip(b'\0').decode('ascii');at=end
 while at+8<=p+8+outer:
  finish=at+8+struct.unpack_from('<I',f,at+4)[0];assert finish<=p+8+outer
  if f[at:at+4]==b'Scrp':
   q=at+8;state='';fi=None;target=None
   while q<finish:
    tag=f[q:q+2];size=struct.unpack_from('<H',f,q+2)[0];assert q+4+size<=finish
    v=f[q+4:q+4+size]
    if tag==b'Sn':state=v.rstrip(b'\0').decode('ascii')
    if tag==b'FI':fi=v
    if tag==b'To':target=struct.unpack('<I',v)[0]
    if tag==b'Kd' and (name,state) in wanted and fi[0]==6 and target==0:
     assert fi[:4]==bytes((6,16,1,0)) and len(v)%20==0
     seen.add((name,state));keys=[v[k:k+20] for k in range(20,len(v),20)]
     for tick,expected in wanted[name,state].items():
      m=Machine(im);frame=0x1001000;out=0x1002000;nodes=[0x1003000+i*64 for i in range(len(keys))]
      m.uc.mem_write(frame,fi);m.uc.mem_write(frame+0x18,v[4:20])
      m.write(frame+0x30,nodes[0]);m.write(frame+0x34,nodes[-1])
      for i,node in enumerate(nodes):
       m.write(node+4,nodes[i+1] if i+1<len(nodes) else 0);m.write(node+8,nodes[i-1] if i else 0);m.uc.mem_write(node+12,keys[i])
      m.run(0x4fc100,(frame,tick,out));rgba=struct.unpack('<4i',m.uc.mem_read(out,16))
      assert rgba[3]==expected,(name,state,tick,rgba,expected)
      print('NATIVE_TEXT',name,state,tick,rgba)
    q+=4+size
  at=finish
 p=end
assert seen==set(wanted)
print('PASS: authenticated nonloop text Init/FadeIn zero/midpoint/endpoint keys; supplied phase; main selection consumer4B371D..4B3766 dispatch separately traced')
