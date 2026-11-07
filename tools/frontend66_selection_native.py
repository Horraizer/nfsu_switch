"""Original two-mode selection script arithmetic/start/child color witness.
Phase and parent state are explicit boundaries, not complete retail startup.
"""
from pathlib import Path
import sys,struct,hashlib,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from event0_trace import Image,jdlz
from hud254_native import Machine
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
root=parser.parse_args().retail_root
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
f=jdlz((root/'FrontEnd/FrontB.lzc').read_bytes());assert hashlib.sha256(f).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
o=0xbec98c;f=jdlz(f[o:o+struct.unpack_from('<I',f,o+12)[0]])
for start in (0x7f24,0x8000,0x794c,0x7a28,0x5890,0x5958,0x4a20,0x4ae8):
 end=start+8+struct.unpack_from('<I',f,start+4)[0];p=start+8;tracks=[]
 while p<end:
  tag=f[p:p+2];n=struct.unpack_from('<H',f,p+2)[0];v=f[p+4:p+4+n]
  if tag==b'FI':tracks.append([v,None])
  if tag==b'Kd':tracks[-1][1]=v
  p+=4+n
 for fi,kd in tracks:
  stride=16 if fi[0]==4 else 20
  for tick in (0,20,100,200):
   m=Machine(im);frame=0x1001000;out=0x1002000
   m.uc.mem_write(frame,fi);m.uc.mem_write(frame+0x18,kd[4:stride])
   keys=[kd[p:p+stride] for p in range(stride,len(kd),stride)]
   nodes=[0x1003000+i*64 for i in range(len(keys))]
   m.write(frame+0x30,nodes[0] if nodes else 0);m.write(frame+0x34,nodes[-1] if nodes else 0)
   for i,node in enumerate(nodes):
    m.write(node+4,nodes[i+1] if i+1<len(nodes) else 0);m.write(node+8,nodes[i-1] if i else 0);m.uc.mem_write(node+12,keys[i])
   m.run(0x4fc100,(frame,tick,out))
   result=struct.unpack('<4i' if fi[0]==6 else '<3f',m.uc.mem_read(out,16 if fi[0]==6 else 12))
   print(hex(start),fi.hex(),tick,result)
# Execute actual start helper: authored FI interpolation mode1 is deliberately
# excluded from the dynamic rebase branch used by modes3/4.
m=Machine(im);obj=0x1001000;script=0x1002000;frame=0x1003000;node=0x1004000
m.write(obj+0x54,script);m.write(script+0x1c,1);m.write(script+0x20,frame)
m.uc.mem_write(frame,bytes((6,16,1,0))+struct.pack('<I',200))
m.uc.mem_write(frame+0x18,struct.pack('<4i',255,255,255,255));m.write(frame+0x30,node)
m.uc.mem_write(node+0x10,struct.pack('<4i',0,0,0,-245))
before=bytes(m.uc.mem_read(frame,56))+bytes(m.uc.mem_read(node,32))
m.run(0x4ff030,(obj,))
after=bytes(m.uc.mem_read(frame,56))+bytes(m.uc.mem_read(node,32))
assert before==after
print('PASS genuine4FF030 excludes authored interpolationmode1 from rebasing')
print('PASS genuine nonloop selection arithmetic; supplied script phase, event dispatch still separate')

from hud254_presentation_native import Renderer
objects={};p=0
while p+16<len(f):
 if f[p:p+4]!=b'FOb\xea':p+=1;continue
 end=p+16+struct.unpack_from('<I',f,p+12)[0];q=p+16;fields={}
 while q<end:
  n=struct.unpack_from('<H',f,q+2)[0];fields[f[q:q+2]]=f[q+4:q+4+n];q+=4+n
 objects[struct.unpack_from('<I',fields[b'OP'])[0]]=fields;p=end
for child,parent in ((0xd30e0,0xd30df),(0xd30ef,0xd30ee)):
 fields=objects[child];sa=objects[parent][b'SA'];position=struct.unpack_from('<3f',sa,28)
 for alpha in (10,35,133,255,230,128,0):
  matrix=[1.,0,0,0,0,1.,0,0,0,0,1.,0,175.,position[1],56.,1.]
  obj={'fields':[{'tag':tag.decode('ascii'),'raw':value.hex()} for tag,value in fields.items()],'default_resource':{'name':'White16x16'}}
  r=Renderer(im);r.object_fields(obj,'',parent={'matrix':matrix,'color':[255,255,255,alpha]});result=r.render_image((16,16))
  quad=result['local']['quads'][0]
  native_alpha={10:'02',35:'08',133:'20',255:'3E',230:'38',128:'1F',0:'00'}
  assert quad['colors']==[native_alpha[alpha]+'624302']*4
  y=104. if child==0xd30e0 else 146.
  assert quad['positions']==[[338.,y,1.],[585.25,y,1.],[585.25,y+20,1.],[338.,y+20,1.]]
  print('NATIVE_SELECTION_CHILD',hex(child),alpha,quad)
print('PASS: genuine selection key/start guard and original child corners/parent colors')
