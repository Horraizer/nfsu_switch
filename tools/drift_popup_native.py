"""Actual drift item0 publication4A85E0/4A6EB0 and display4A83C0.
Real FEng color/show/hide code executes on declared local appearance records.
Only the variadic text OUTPUT setter is captured. This is state and setter
consumption evidence, not original font/layout/rendering or a whole game.
"""
from pathlib import Path
import sys,json,hashlib,argparse
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('retail_root',type=Path);parser.add_argument('--write-fixture',action='store_true');args=parser.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
def case(zone):
 m=Machine(Image(raw));m.allowed=[(0x400000,0x6a0000)]
 head,item,style,view,hud,drift,rules=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000,0x1106000
 m.put(0x78a345,1,'B');m.put(0x736198,rules);m.put(rules,0xa46aaea8);m.put(rules+6,1,'B')
 m.put(head,item);m.put(head+4,item);m.put(item,head);m.put(item+4,head);m.put(item+0xc,0);m.put(item+0x18,2)
 m.put(style+0x20,style+0x20);m.put(style+0x24,style+0x20);m.put(style+0x10,1);m.put(style+0x18,1)
 m.put(0x7361b4,view);m.put(view+0x95c,hud);m.put(hud+0x2c,drift)
 for base,off,index in [(style,0x3c,0),(style,0x40,1),(drift,0x88,2),(drift,0x8c,3),(drift,0x90,4)]:
  m.put(base+off,0x1110000+index*0x1000);m.put(0x1110000+index*0x1000+0x2c,0x1120000+index*0x1000)
 m.put(style+0x50,0xffff8020 if zone>1 else 0xffffff00)
 # Text formatting/output is captured; score state, visibility/color, enqueue
 # and expiration code execute. No FEng layout or rendering is claimed.
 formats=[]
 def output(m):
  sp=m.reg('esp');fmt=m.get(sp+4);formats.append(dict(format=hex(fmt),value=m.get(sp+8)));m.ret()
 m.hook(0x4f68a0,'FEng_text_output',callback=output)
 
 frames=[]
 for now,phase,points in [(10000,0,123.75),(10001,0,456.),(10002,3,456.),(10003,None,0.),(14003,None,0.),(15000,0,200.),(15001,4,0.),(19001,None,0.),(19002,None,0.)]:
  m.put(0x73ad34,now)
  if phase is not None:
   m.put(item+0x10,phase);m.put(item+0x20,points,'f');m.run(0x4a85e0,(style,),eax=head)
  before=dict(points=m.get(drift+0x2c,'f'),multiplier=m.get(drift+0x30),present=m.get(drift+0x40,'B'),failed=m.get(drift+0x41,'B'),shown=m.get(drift+0x38))
  m.run(0x4a83c0,ebx=style)
  frames.append(dict(now=now,phase=phase,input=points,before=before,after=dict(points=m.get(drift+0x2c,'f'),present=m.get(drift+0x40,'B'),failed=m.get(drift+0x41,'B'),shown=m.get(drift+0x38),visible=not bool(m.get(0x1113000+0x1c)&1),color=[m.get(0x1123000+4*k)for k in range(4)]),formats=formats[:]))
  formats.clear()
 return frames
rows=[]
for zone in (1,2):
 for frame in case(zone):
  before=frame['before'];after=frame['after'];b,g,r,a=after['color']
  color=(r<<24)|(g<<16)|(b<<8)|a
  rows.append([zone,frame['now'],0xffffffff if frame['phase'] is None else frame['phase'],bits(frame['input']),bits(before['points']),before['multiplier'],before['failed'],int(after['visible']),color,frame['formats'][-1]['value']if frame['formats']else 0])
text='// Actual4A85E0/4A6EB0/4A83C0; only text output captured, real color/visibility.\nstatic constexpr unsigned kNativeDriftPopup[][10]={\n'
for row in rows:text+=' {'+','.join('0x%08XU'%v for v in row)+'},\n'
text+='};\n'
p=Path(__file__).resolve().parents[1]/'tests/drift_popup_native.inc'
if args.write_fixture:p.write_text(text,encoding='ascii')
else:assert p.read_text('ascii')==text,'original drift popup fixture differs'
print('PASS original drift popup publication, retained loss, lifetime and color, rows='+str(len(rows)))
