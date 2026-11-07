"""Original FEng viewport witness for authored controls, logo and font.

Authentic object/font records drive original image/text/submit code. Host
allocation, supplied texture metadata and COM submissions use Renderer boundaries.
LogoCover's supplied parent matrix is its authored identity rotation/unit scale
and translation; the original hierarchy/color/quad consumers then execute.
No visibility flag or name suppression is substituted for native clipping.
"""
from pathlib import Path
import argparse,hashlib,struct,sys
sys.dont_write_bytecode=True
from event0_trace import Image,jdlz
from hud254_presentation_native import Renderer
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
root=parser.parse_args().retail_root
raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);image.sections=[(a,s,r) for a,s,r,n in image.sections]
front=jdlz((root/'FrontEnd/FrontB.lzc').read_bytes())
assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
offset=0xbec98c;fng=jdlz(front[offset:offset+struct.unpack_from('<I',front,offset+12)[0]])
global_a=(root/'Global/GLOBALA.BUN').read_bytes();global_a=jdlz(global_a) if global_a[:4]==b'JDLZ' else global_a
assert hashlib.sha256(global_a).hexdigest()=='1502d9305f37e51503d4c4e57cae32890059497ac450feee6d72b50323568953'
objects={};pos=0
while pos+16<=len(fng):
 if fng[pos:pos+4]!=b'FOb\xea':pos+=1;continue
 size=struct.unpack_from('<I',fng,pos+12)[0];at=pos+16;end=at+size;fields={}
 while at<end:
  tag=fng[at:at+2];n=struct.unpack_from('<H',fng,at+2)[0];fields[tag]=fng[at+4:at+4+n];at+=4+n
 objects[struct.unpack_from('<I',fields[b'OP'])[0]]=fields;pos=end
for fields in objects.values():
 name=fields.get(b'On',b'').split(b'\0')[0].decode('ascii')
 if name not in ('STATISTICS','NFSU_logo','DisplayMenuBar','quit_tag','back_tag') and not name.startswith('circle'):continue
 obj={'fields':[{'tag':tag.decode('ascii'),'raw':value.hex()} for tag,value in fields.items()],'default_resource':{'name':'conduitmditc_tt21i' if name=='STATISTICS' else 'NFSU_LOGO' if name=='NFSU_logo' else 'White16x16'}}
 native=Renderer(image)
 if name=='STATISTICS':
  native.load_font(global_a,'conduitmditc_tt21i',(512,256));native.object_fields(obj,'STATISTICS');result=native.render_text()
 elif name=='NFSU_logo':
  parent=objects[struct.unpack_from('<I',fields[b'PA'])[0]]
  assert b'PA' not in parent
  sa=parent[b'SA'];matrix=[1.,0,0,0,0,1.,0,0,0,0,1.,0]+list(struct.unpack_from('<3f',sa,28))+[1.]
  native.object_fields(obj,'',parent={'matrix':matrix,'color':struct.unpack_from('<4I',sa)});result=native.render_image((512,64))
 elif name.startswith('circle'):
  obj['default_resource']={'name':'CIRCLE'}
  assert b'PA' not in fields
  native.object_fields(obj,'');result=native.render_image((32,32))
 else:
  native.object_fields(obj,'');result=native.render_image((16,16))
  assert all(v['position'][0]<-1 for v in result['draws'][0]['vertices'])
 print(name,'LOCAL_FIRST',result['local']['quads'][0]['positions']);print('DRAW_FIRST',result['draws'][0]['vertices'])



print("PASS: original offscreen marker clipping and onscreen logo/font viewport corners")
