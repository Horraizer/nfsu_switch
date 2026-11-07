"""Bounded original FEng loop interpolation witness from authored circle Init."""
from pathlib import Path
import argparse,hashlib,struct,sys
sys.dont_write_bytecode=True
from event0_trace import Image,jdlz
from hud254_native import Machine
from hud254_presentation_native import Renderer
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--script-offset',type=lambda x:int(x,0),default=0x9b74)
parser.add_argument('--object-offset',type=lambda x:int(x,0),default=None)
args=parser.parse_args();root=args.retail_root
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);image.sections=[(a,s,r) for a,s,r,n in image.sections]
front=jdlz((root/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
o=0xbec98c;fng=jdlz(front[o:o+struct.unpack_from('<I',front,o+12)[0]])
# Authenticated circle Init package, with original type6 RGBA/type5 quaternion.
start=args.script_offset;assert fng[start:start+4]==b'Scrp'
end=start+8+struct.unpack_from('<I',fng,start+4)[0];p=start+8;tracks=[]
while p<end:
 tag=fng[p:p+2];n=struct.unpack_from('<H',fng,p+2)[0];data=fng[p+4:p+4+n]
 if tag==b'FI':tracks.append([data,None])
 if tag==b'Kd':tracks[-1][1]=data
 p+=n+4
assert [x[0][0] for x in tracks]==[6,5]
results={}
for fi,kd in tracks:
 duration=struct.unpack_from('<I',fi,4)[0]
 assert fi==bytes((fi[0],16,1,1))+struct.pack('<I',duration)
 assert len(kd)==80
 for tick in tuple(duration*i//8 for i in range(8))+(duration-1,):
  m=Machine(image);frame=0x1001000;out=0x1002000;nodes=[0x1003000+i*64 for i in range(3)]
  m.uc.mem_write(frame,fi)
  m.uc.mem_write(frame+0x18,kd[4:20])
  m.write(frame+0x30,nodes[0]);m.write(frame+0x34,nodes[-1])
  for i,node in enumerate(nodes):
   m.write(node+4,nodes[i+1] if i<2 else 0);m.write(node+8,nodes[i-1] if i else 0)
   m.uc.mem_write(node+0xc,kd[20+i*20:40+i*20])
  phases=[]
  def trace(uc,at,size,_):
   if at==0x564b10:phases.append(uc.reg_read(UC_X86_REG_EAX)&65535)
  m.uc.hook_add(UC_HOOK_CODE,trace)
  m.run(0x4fc100,(frame,tick,out))
  result=struct.unpack('<4i' if fi[0]==6 else '<4f',m.uc.mem_read(out,16))
  results[fi[0],tick]=result
  print('TYPE',fi[0],'TICK',tick,'NATIVE',result,'SINE_PHASES',phases)

# Submit interpolated state through the genuine image transform, independently
# of the source renderer's rotation/viewport math.
objstart=args.object_offset if args.object_offset is not None else fng.rfind(bytes((70,79,98,234)),0,start);n=struct.unpack_from('<I',fng,objstart+12)[0];p=objstart+16;fields={}
while p<objstart+16+n:
 tag=fng[p:p+2];size=struct.unpack_from('<H',fng,p+2)[0]
 fields[tag]=fng[p+4:p+4+size];p+=size+4
for tick in (0,duration//4,duration//2,duration*3//4,duration-1):
 sa=bytearray(fields[b'SA']);struct.pack_into('<4I',sa,0,*results[6,tick]);struct.pack_into('<4f',sa,40,*results[5,tick])
 sample=dict(fields);sample[b'SA']=sa
 obj={'fields':[{'tag':k.decode('ascii'),'raw':v.hex()} for k,v in sample.items()],'default_resource':{'name':'CIRCLE'}}
 native=Renderer(image);native.object_fields(obj,'');actual=native.render_image((32,32))
 print('CIRCLE_IMAGE_TICK',tick,'LOCAL',actual['local']['quads'][0]['positions'])

# Native frontend frame clock writer4F5CF0 reads frame seconds73AD38 and
# multiplies6CCDE4 before the original CRT integer conversion.
for va,size,offset in image.sections:
 if va<=0x6ccde4<va+size:
  factor=struct.unpack_from('<f',raw,offset+0x6ccde4-va)[0]
  assert factor==1200.0
print('PASS: genuine FEng loop color/quaternion/image interpolation and authenticated UI clock factor; supplied phase ticks')
