"""Actual4576B0 on explicitly supplied pair states, zero semantic query hooks.

Original stock factory/body setup precedes declared replacement pose, retained
owner speeds, action and two-point Driver paths. The actual57A530 publisher,
4576B0 branches, path projection, cursor, hull getters/shapes and complete
walk execute. This does not certify a natural trajectory or action lifecycle.
"""
from pathlib import Path
import sys,hashlib,json,capstone
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from controller293_planner import inputs,CTX
from controller293_scene import PairedController,HUMAN
from controller293_strategy import CONTROLLER
from physics256_native import ACTOR,DRIVER,bits
import argparse
ap=argparse.ArgumentParser(description='Actual complete4576B0 on explicit stock-body pair states; no natural trajectory or action-owner claim.')
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path);options=ap.parse_args()
retail=options.retail;raw=(retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
pair=PairedController(Image(raw),inputs(retail));m=pair.m
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);image=Image(raw)
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x4338c0,0x560),0x4338c0)})
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x59c7b0,0x6b),0x59c7b0)})
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x59c210,0x250),0x59c210)})
for a,b in((0x456ce0,0x456d46),(0x456de0,0x456e8a),(0x458f10,0x459096),(0x4594d0,0x4596b0),(0x564bd0,0x564cd0),(0x567370,0x5673e0)):
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x429c50,0x64),0x429c50)})
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x4576b0,0xaa0),0x4576b0)})
frame=pair.step(31,0.)
m=pair.m
for a,b in((0x57a530,0x57a5ec),(0x456d50,0x456de0),(0x57a820,0x57a8e0),(0x44d5f0,0x44d640),(0x45a250,0x45a2f0),(0x45a2f0,0x45a4c2)):
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
# Actual precontroller publication57A530 for the next declared input epoch.
m.put(0x73ad34,m.get(0x73ad34)+80)
node=m.get(0x73b07c);visited=[]
while node!=0x73b07c:
 assert node not in visited;visited.append(node);m.run(0x57a530,ebx=node,budget=3000000);node=m.get(node)

node=m.get(CTX+0x3c)
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x5672b0,0x54),0x5672b0)})
m.instructions.update({i.address:i.size for i in md.disasm(image.read(0x459710,0x157),0x459710)})

for a,b in ((0x458750,0x458a4a),(0x4586a0,0x45874d),(0x4584f0,0x458692)):
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
# Explicit finite pair-input states on already constructed stock world bodies.
# This replaces supplied pose/controller/path state, not executable behavior.
from unicorn import UC_HOOK_CODE
import itertools,math
human_vtable=m.get(m.get(HUMAN+0x2c))
other=HUMAN;other_controller=0x1190000;other_driver=0x1191000;other_action=0x1192000
m.put(other+0x24,3);m.put(other+0x2c,other_controller);m.put(other+0x30,other_driver)
m.put(other_controller,m.get(CONTROLLER));m.put(other_controller+4,other);m.put(other_controller+8,other_driver);m.put(other_controller+0x3f8,other_action)
m.put(m.get(other+0x14)+4,2)
self_node=m.get(CTX+0x3c+0x38);self_proxy=m.get(self_node+0x1d4);other_proxy=m.get(node+0x1d4)
assert m.get(self_proxy+0x90)==ACTOR and m.get(other_proxy+0x90)==other
out=0x119f000;rows=[];boxes=[]
def observe(uc,address,size,unused):
 if address==0x458340:boxes.append([[m.get(m.reg(reg)+4*k) for k in range(3)] for reg in ('ecx','eax')])
m.uc.hook_add(UC_HOOK_CODE,observe,begin=0x458340,end=0x458340)
def point(at,vector):
 for k,v in enumerate(vector):m.put(at+4*k,float(v),'f')
def state(actor,proxy,x,y,speed,magnitude):
 position=(x,y,0.);velocity=(speed,0.,0.);body=m.get(proxy+0x1c)
 point(actor+0x60,position);point(actor+0x70,velocity);point(actor+0x80,(0.,0.,0.));point(actor+0x90,(1.,0.,0.))
 m.put(actor+0x3d0,speed,'f');m.put(actor+0x3d4,magnitude,'f');m.put(actor+0x3b4,0,'H')
 for a,v in ((body+0x10,(1.,0.,0.)),(body+0x20,(0.,1.,0.)),(body+0x30,(0.,0.,1.)),(body+0x40,position),(body+0x90,velocity)):point(a,v)
 m.put(body+0x4c,1.,'f');m.put(body+0xe4,0)
def path(at,x,y):
 m.uc.mem_write(at,b'\0'*0x1c0);m.put(at,2,'B');point(at+0x10,(x+100.,y,0.));point(at+0x20,(x+200.,y,0.))
 for j in range(2):m.put(at+0x150+j,255,'B');m.put(at+0x164+2*j,65535,'H');m.put(at+0x18c+j,255,'B')
 m.put(at+0x1ac,100.,'f')
for x,y,speed_a,speed_b,owner_mag,action_b,role in itertools.product((20.,60.,100.,160.,200.),(0.,4.),(20.,-20.),(10.,-20.),(20.,80.),(7,10),(1,2)):
 m.put(0x73ad34,m.get(0x73ad34)+80)
 m.put(other+0x24,1 if role==1 else 3);m.put(m.get(other+0x14)+4,role)
 m.put(other_controller,human_vtable if role==1 else m.get(CONTROLLER))
 state(ACTOR,self_proxy,0.,0.,speed_a,20.)
 state(other,other_proxy,x,y,speed_b,owner_mag)
 m.put(m.get(CONTROLLER+0x3f8)+0x1f8,10 if speed_a<0 else 7)
 m.put(other_action+0x1f8,action_b)
 path(DRIVER+0x10,0.,0.);path(other_driver+0x10,x,y)
 m.run(0x57a530,ebx=node,budget=3000000)
 m.events.clear();boxes.clear();m.put(out,bits(42.))
 m.run(0x4576b0,(ACTOR,DRIVER+0x10,CTX+0x38c,ACTOR,bits(4.),out),ecx=node,budget=3000000)
 assert not m.events,m.events
 assert len(boxes)==2,boxes
 rows.append([[bits(v)for v in (x,y,speed_a,speed_b,owner_mag)],action_b,role,boxes,m.reg('eax')&255,m.get(out)])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual complete4576B0 on explicit pair inputs; bootstrap uses stock world bodies.\n'
text+='struct NativeWorldPair {unsigned motion[5],action,role,boxes[2][2][3],hit,time;};\n'
text+='static const NativeWorldPair kNativeWorldPair[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/world_pair_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'actual complete world pair fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(root.resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=ap.description,rows=rows),indent=2)+'\n',encoding='ascii')
print('PASS actual complete world pair states',len(rows),'hits',sum(c[-2]for c in rows))
