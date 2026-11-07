"""Actual intersection primitives, with explicit synthetic input state.

No semantic IO is replaced and no retail bytes are patched. The ordinary
traffic caller, chunk parser, factory, spawn and physics are separate owners.
"""
from pathlib import Path
import sys,hashlib,json,itertools
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine,ACTOR,bits
import argparse
ap=argparse.ArgumentParser(description='Actual original intersection lifecycle and permission queries on explicit synthetic state')
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();raw=(options.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7','unsupported executable'
image=Image(raw);I=0x1164000;C=0x1170000;B=ACTOR+0x1000

def fresh():
 m=Machine(image)
 m.allowed=[(0x5aadf0,0x5aae7a),(0x5aae80,0x5aaea0),(0x5aaea0,0x5aaf02),(0x5aaf10,0x5aaf84),(0x5aaca0,0x5aacf2),(0x5aad10,0x5aadf0),(0x44ca70,0x44cb80)]
 m.put(0x736158,I);m.put(0x73615c,1);m.put(C+4,ACTOR)
 m.put(ACTOR+0x14,0x1151000);m.put(B+0x14,0x1152000);m.put(0x1151004,3);m.put(0x1152004,3)
 m.put(I,1);m.put(I+4,101,'b');m.put(I+5,36,'b');m.put(I+6,-1,'b');m.put(I+7,-1,'b');m.put(I+12,0xffffffff)
 for j in range(22):m.put(I+0x14+j*16,0xffffffff)
 return m

def snap(m):
 entries=[[m.get(I+0x10+j*16,'B'),m.get(I+0x14+j*16,'i'),m.get(I+0x18+j*16),m.get(I+0x1c+j*16)]for j in range(22)]
 return [m.get(I,'i'),[m.get(I+4+k,'b')for k in range(2)],[m.get(I+6+k,'b')for k in range(2)],m.get(I+8),m.get(I+12,'i'),entries]

def register(m,identity,role,route,distance,time):
 m.put(ACTOR+10,identity,'h');m.put(0x1151004,role)
 m.run(0x5aadf0,(bits(distance),bits(time)),eax=ACTOR,ecx=route&0xffffffff,edx=I)

rows=[];m=fresh()
sequence=[(50,3,101,20.,3.),(51,3,101,10.,3.),(71,3,101,10.,2.999999761581421),(52,3,36,12.,2.),(53,3,99,9.,1.),(0,1,101,0.,0.),(54,3,36,11.999999046325684,1.),(55,3,36,-1.,0.)]
for args in sequence:
 before=snap(m);register(m,*args)
 rows.append(dict(op='register',args=list(args[:3])+[bits(args[3]),bits(args[4])],before=before,after=snap(m)))
for identity in (71,50,55,54,52,51,53):
 m.put(ACTOR+10,identity,'h');before=snap(m);m.run(0x44ca70,ebx=C)
 rows.append(dict(op='clear',args=[identity],before=before,after=snap(m)))
for selected,distance in itertools.product((-1,0,1),(11.999999046325684,12.,12.000000953674316,-1.)):
 m=fresh();register(m,50,3,101,distance,2.);register(m,51,3,36,20.,1.)
 m.put(I+12,selected&0xffffffff);before=snap(m);m.run(0x5aaf10,esi=I)
 rows.append(dict(op='step',args=[],before=before,after=snap(m)))
queries=[]
for mode,selected,direction,time in itertools.product((0,1,2,3,4),(-1,0,1),(0,1),(2.999999761581421,3.,3.000000238418579)):
 m=fresh();register(m,50,3,101,10.,time);m.put(I,mode);m.put(I+12,selected&0xffffffff)
 m.run(0x5aaca0,(0,),esi=I,edx=direction)
 queries.append(dict(op='open',args=[direction],state=snap(m),result=m.reg('eax')&255))
for r1,r2,d1,d2,x1,x2,active in itertools.product((1,3),(1,3),(0,1),(0,1),(-1.,0.),(-1.,0.),(False,True)):
 m=fresh();register(m,50,3,101 if d1==0 else 36,x1,1.);register(m,71,3,101 if d2==0 else 36,x2,2.)
 m.put(ACTOR+10,50,'h');m.put(B+10,71,'h');m.put(0x1151004,r1);m.put(0x1152004,r2)
 if not active:m.put(I+0x10+21*16,0,'B')
 m.run(0x5aad10,eax=ACTOR,ecx=B,esi=I)
 queries.append(dict(op='pair',args=[50,r1,71,r2],state=snap(m),result=m.reg('eax')&255))
def lit(v):
 if isinstance(v,(list,tuple)):return '{'+','.join(map(lit,v))+'}'
 return str(v)
text='// Actual5AADF0/44CA70/5AAF10/5AACA0/5AAD10, declared synthetic state.\n'
text+='struct NativeIntersectionEntry {unsigned active;int direction;unsigned distance,time;};\n'
text+='struct NativeIntersectionSnapshot {int kind,routes[2],closest[2];unsigned points_word;int selected;NativeIntersectionEntry actors[22];};\n'
text+='struct NativeIntersectionUpdate {unsigned operation;unsigned args[5];NativeIntersectionSnapshot before,after;};\n'
text+='static const NativeIntersectionUpdate kNativeIntersectionUpdates[]={\n'
for row in rows:
 arguments=[v&0xffffffff for v in row['args']]+[0]*(5-len(row['args']))
 text+=lit([{'register':0,'clear':1,'step':2}[row['op']],arguments,row['before'],row['after']])+',\n'
text+='};\nstruct NativeIntersectionQuery {unsigned operation,args[4];NativeIntersectionSnapshot state;unsigned result;};\n'
text+='static const NativeIntersectionQuery kNativeIntersectionQueries[]={\n'
for row in queries:
 arguments=[v&0xffffffff for v in row['args']]+[0]*(4-len(row['args']))
 text+=lit([{'open':0,'pair':1}[row['op']],arguments,row['state'],row['result']])+',\n'
text+='};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/traffic_intersection_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original intersection fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd());assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
 options.out.mkdir(exist_ok=False)
 (options.out/'oracle.json').write_text(json.dumps(dict(scope='Actual intersection updates/queries, no semantic hooks; synthetic inputs, not natural scheduler/traffic-light visuals',rows=rows,queries=queries),indent=2)+'\n',encoding='ascii')
print('PASS actual original intersection lifecycle and permission queries')
