"""Original traffic world prediction, including the retained Driver path.

The base original world-pair oracle is run and checked first. The subsequent
role3 cases install the actual TrafficController vtable and explicit raw-road,
stock body and Driver records. Complete57A530 and4576B0 instructions execute;
observers read the selected forecast and hull boxes without replacing any
query or collision method. Bootstrap allocator/host boundaries remain those
of world_pair_native.py. This is not natural-race or physical-impulse evidence.
"""
from pathlib import Path
import sys,struct,json,itertools
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
import argparse,contextlib,io,runpy,hashlib
ap=argparse.ArgumentParser(description="Original57A530/4576B0 traffic forecast and full pair query on declared stock bodies, traffic controller, raw road and Driver inputs; no new drive.")
ap.add_argument('retail',type=Path);ap.add_argument('--out',type=Path);ap.add_argument('--write-fixture',action='store_true');cli=ap.parse_args()
base=Path.cwd()/'tools/world_pair_native.py';saved_argv=sys.argv
bootstrap=io.StringIO()
try:
 sys.argv=[str(base),str(cli.retail)]
 with contextlib.redirect_stdout(bootstrap):environment=runpy.run_path(str(base),run_name='__main__')
finally:sys.argv=saved_argv
probe_scope=ap.description
globals().update({k:v for k,v in environment.items()if not k.startswith('__')})
from unicorn import UC_HOOK_CODE
for start,end in ((0x44acb0,0x44acf5),(0x583240,0x583247),(0x45a510,0x45a5b0),(0x45a5b0,0x45a790)):
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(start,end-start),start)})
rawroad,rawpoint=0x1194000,0x1195000
m.put(other+0x24,4);m.put(m.get(other+0x14)+4,3);m.put(other_controller,0x6b96b0);m.put(other_controller+0x3d8,rawroad);m.put(rawroad,1);m.put(rawroad+4,rawpoint)
captured=[]
def forecast(uc,address,size,unused):
 at=m.get(m.reg('esp')+8);n=m.get(at,'B');assert n<=20
 captured.append(dict(count=n,points=[[m.get(at+0x10+16*j+4*k)for k in range(3)]for j in range(n)],length=m.get(at+0x1ac),cursor=m.get(at+0x1a0,'B'),cursor_tick=m.get(at+0x1a4),prediction_tick=m.get(at+0x1a8)))
m.uc.hook_add(UC_HOOK_CODE,forecast,begin=0x458290,end=0x458290)
records=[]
cases=[tuple(v)+(x,y,False)for v in itertools.product((4.,.25,struct.unpack('<f',struct.pack('<I',bits(.25)-1))[0],struct.unpack('<f',struct.pack('<I',bits(.25)+1))[0],0.),(7,8,18),(0.,10.,-10.),(False,True))for x,y in itertools.product((20.,60.,100.),(0.,4.))]+[(1.25,8,10.,curve,20.,0.,True)for curve in (False,True)]
for width,action,speed,curve,x,y,precision in cases:
 m.put(0x73ad34,m.get(0x73ad34)+80);tick=m.get(0x73ad34)
 state(ACTOR,self_proxy,0.,0.,20.,20.);state(other,other_proxy,x,y,speed,abs(speed));m.put(other_action+0x1f8,action)
 point(rawpoint,(x,y,0.));point(rawpoint+0x0c,(1.,0.,0.));m.put(rawpoint+0x24,width,'f');m.put(rawpoint+0x28,width,'f')
 outside=-width
 if precision:
  point(rawpoint,(x-1.25,y-2.**-26,0.));point(rawpoint+0x0c,(.6,.8,0.))
  f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
  # The original runs x87 with control word007F: each arithmetic
  # operation rounds to24-bit precision, not the decompiler float10.
  lateral=f32(f32(f32(20.-18.75)*f32(.8))-f32(f32(2.**-26)*f32(.6)))
  outside=f32(lateral-width)
 path(DRIVER+0x10,0.,0.);path(other_driver+0x10,x,y)
 if curve:
  at=other_driver+0x10;m.put(at,3,'B');point(at+0x10,(x+20.,y,0.));point(at+0x20,(x+40.,y,0.));point(at+0x30,(x+40.,y+20.,0.));m.put(at+0x1ac,40.,'f');m.put(at+0x152,255,'B');m.put(at+0x168,65535,'H');m.put(at+0x18e,255,'B')
 m.run(0x57a530,ebx=node,budget=3000000);m.events.clear();boxes.clear();captured.clear();m.put(out,bits(42.))
 m.run(0x4576b0,(ACTOR,DRIVER+0x10,CTX+0x38c,ACTOR,bits(4.),out),ecx=node,budget=3000000)
 assert not m.events,m.events;assert len(captured)==1,captured
 records.append(dict(position=[x,y],width=width,outside=outside,precision=precision,action=action,speed=speed,curve=curve,tick=tick,forecast=captured[0],boxes=boxes,hit=m.reg('eax')&255,time=m.get(out)))
def literal(value):
 if isinstance(value,list):return '{'+','.join(map(literal,value))+'}'
 return value.hex()if isinstance(value,float)else str(int(value))
fixtures=[]
for item in records:
 f=item['forecast'];points=f['points']+[[0,0,0]for unused in range(20-f['count'])]
 fixtures.append([[bits(v)for v in item['position']],bits(item['width']),item['outside'],item['action'],bits(item['speed']),item['curve'],item['tick'],f['count'],points,f['length'],f['cursor'],f['cursor_tick'],f['prediction_tick'],item['boxes'],item['hit'],item['time']])
text='// Actual4576B0 role3; native vtable28/50, raw-road and Driver inputs; no query hook.\nstruct NativeTrafficWorldPair {unsigned position[2],width;double outside;unsigned action,speed,curve,tick,count,points[20][3],length,cursor,cursor_tick,prediction_tick,boxes[2][2][3],hit,time;};\nstatic const NativeTrafficWorldPair kNativeTrafficWorldPair[]={\n'+''.join(literal(row)+',\n'for row in fixtures)+'};\n'
fixture=Path.cwd()/'tests/traffic_world_path_native.inc';assert text.isascii()
if cli.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'traffic world caller fixture differs'
if cli.out:
 relative=cli.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not cli.out.exists(),'refusing stale output';cli.out.parent.mkdir(parents=True,exist_ok=True)
 cli.out.write_text(json.dumps(dict(scope=probe_scope,bootstrap_source_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),bootstrap_check=bootstrap.getvalue(),rows=records),indent=2)+'\n',encoding='ascii')
print('PASS original traffic world forecast and complete pair queries',len(records))
