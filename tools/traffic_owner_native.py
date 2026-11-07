"""Actual traffic ownership58350D, role3 gate583350 and strategy44E020.

Virtual predicates, geometry results, constructors/allocation/destruction,
reaction, physics construction and final path/speed publication are explicit
boundary witnesses. This certifies caller order, not their implementations
or a natural race. Retail instructions and files remain unchanged.
"""
from pathlib import Path
import sys,hashlib,json,itertools,argparse
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine,ACTOR,bits
from unicorn.x86_const import UC_X86_REG_EIP
CONTROLLER=0x1160000
ACTIONS=[0,0x1161000,0x1162000,0x1163000]
VTABLE=0x1170000
CALLS=[0x1180000+16*i for i in range(7)]
STRATEGY=0x1164000

def owner(image,mask,timed,kind,priority,present,selector):
 m=Machine(image);events=[]
 m.allowed=[(0x58350d,0x5835db),(0x44f700,0x44f741),(0x44a0e0,0x44a103)]
 m.put(CONTROLLER+4,ACTOR);m.put(CONTROLLER+0x3f8,ACTIONS[1]if present else 0)
 m.put(CONTROLLER+0x3fc,STRATEGY);m.put(STRATEGY,VTABLE)
 m.put(VTABLE+0x14,CALLS[3]);m.put(VTABLE+0x18,CALLS[6]);m.put(ACTOR+0x28,selector)
 for j,at in enumerate(ACTIONS[1:3],1):
  vt=VTABLE+0x100*j;m.put(at,vt)
  for off,op in ((0,5),(12,4),(0x24,0),(0x20,1),(0x1c,2)):m.put(vt+off,CALLS[op])
  m.put(at+0x1f8,8 if j==1 else kind);m.put(at+0x1f4,100 if j==1 else priority)
  m.put(at+4,int(timed if j==1 else False),'B')
 def callback(op):
  def invoke(x):
   at=x.reg('ecx');sp=x.reg('esp')
   if op==3:
    assert at==STRATEGY and x.get(sp+4)==CONTROLLER+0x2c
    events.append([3,x.get(sp+8,'i'),x.get(sp+12,'i')]);x.ret(12,ACTIONS[2])
   elif op==6:events.append([6,0,0]);x.ret(0,ACTIONS[2])
   else:
    handle=ACTIONS.index(at);events.append([op,handle,0]);x.ret(4,int(bool(mask&(1<<op)))if op<3 else None)
  return invoke
 for op,address in enumerate(CALLS):m.hook(address,'declared-owner-boundary-'+str(op),callback=callback(op))
 def promote(x):
  assert x.reg('esi')==ACTOR and x.reg('ecx')==3
  events.append([7,2,3]);x.put(ACTOR+0x28,3);x.ret()
 def update(x):
  assert x.reg('eax')==CONTROLLER
  events.append([8,ACTIONS.index(x.get(CONTROLLER+0x3f8)),0]);x.ret()
 m.hook(0x426110,'physical-construction-boundary',callback=promote)
 m.hook(0x583850,'traffic-path-speed-publication-boundary',callback=update)
 m.run(0x58350d,end=0x5835db,ebp=CONTROLLER)
 at=m.get(CONTROLLER+0x3f8)
 return dict(input=[mask,int(timed),kind,priority,int(present),selector],result=[ACTIONS.index(at),m.get(at+0x1f8),m.get(at+0x1f4,'i'),m.get(at+0x10),m.get(ACTOR+0x28)],calls=events)

def gate(image,count,slot,frame):
 m=Machine(image);world=0x1178000;config=0x1177000
 m.allowed=[(0x583350,0x5833d4),(0x5835e2,0x5835e9)]
 m.put(CONTROLLER+4,ACTOR);m.put(ACTOR+0x14,config);m.put(config+4,3)
 m.put(CONTROLLER+0x3f0,0.,'f');m.put(0x7361f8,world)
 m.put(world+0x24,4);m.put(world+0x18,4+count)
 for j in range(count):m.put(world+0x28+4*(4+j),ACTOR if j==slot else ACTOR+0x1000*(j+1))
 m.put(0x6f08a4,frame)
 m.hook(0x583da0,'contact-stop-refresh-boundary')
 full=[]
 def admitted(x):full.append(True);x.uc.reg_write(UC_X86_REG_EIP,0x1000000)
 m.hook(0x5833d4,'admitted-controller-terminal',callback=admitted)
 m.run(0x583350,(bits(.02),),ecx=CONTROLLER)
 return [count,slot,frame,int(bool(full))]

def strategy(image,kind,priority,obstacle,avoid):
 m=Machine(image);events=[];ctx=CONTROLLER+0x2c
 m.allowed=[(0x44e020,0x44e137)]
 m.put(ctx,ACTOR);m.put(ACTOR+0x2c,CONTROLLER);m.put(STRATEGY,VTABLE);m.put(VTABLE+0x18,CALLS[6])
 def query18(x):
  assert x.reg('eax')==ctx
  events.append(18);x.put(x.reg('esi'),0x41200000);x.put(x.reg('edi'),0x123)
  x.ret(0,600 if obstacle else 0xffffffff)
 def query3(x):
  assert x.reg('esi')==ctx
  events.append(3);sp=x.reg('esp')
  for k,value in enumerate((0x123,0x41200000,0x40a00000)):x.put(x.get(sp+4+4*k),value)
  x.ret(0,300 if avoid else 0xffffffff)
 def initial(x):events.append(8);x.ret(0,ACTIONS[2])
 def allocate(x):events.append(20);x.ret(0,ACTIONS[2])
 def reaction(x):events.append(30);x.ret(8)
 def construct18(x):events.append(118);assert x.reg('ecx')==600;x.ret(12,ACTIONS[2])
 def construct3(x):events.append(103);assert x.reg('ecx')==300;x.ret(16,ACTIONS[2])
 for address,label,callback in ((0x4543d0,'declared-road-obstacle-query',query18),(0x453920,'declared-avoidance-query',query3),(CALLS[6],'declared-default-constructor',initial),(0x567160,'declared-action-allocation',allocate),(0x44c2e0,'declared-actor-reaction',reaction),(0x454170,'declared-obstacle-constructor',construct18),(0x453860,'declared-avoidance-constructor',construct3)):
  m.hook(address,label,callback=callback)
 m.run(0x44e020,(ctx,kind,priority&0xffffffff),ecx=STRATEGY)
 assert m.reg('eax')==ACTIONS[2]
 selected=18 if 118 in events else 3 if 103 in events else 8
 return dict(input=[kind,priority,int(obstacle),int(avoid)],kind=selected,calls=events)

def literal(value):
 if isinstance(value,(list,tuple)):return '{'+','.join(literal(v)for v in value)+'}'
 return str(value)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
 ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
 args=ap.parse_args();raw=(args.retail/'speed.exe').read_bytes()
 identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
 assert hashlib.sha256(raw).hexdigest()==identity,'unsupported executable'
 image=Image(raw)
 rows=[owner(image,mask,timed,kind,priority,True,selector)for mask,timed,kind,priority,selector in itertools.product((0,1,2,4,7),(False,True),(1,3,8,9,18),(0,100,600),(2,3))]
 rows += [owner(image,0,False,kind,priority,False,selector)for kind,priority,selector in itertools.product((1,3,8,9,18),(0,600),(2,3))]
 gates=[gate(image,count,slot,frame)for count in (1,2,6,12)for slot in range(count)for frame in [*range(2*count+2),0x7fffffff]]
 strategies=[strategy(image,kind,priority,obstacle,avoid)for kind,priority,obstacle,avoid in itertools.product((3,8,18,20),(-1,0,299,300,301,599,600,601),(False,True),(False,True))]
 text='// Actual traffic owner/strategy/gate; declared external boundaries.\n'
 text+='struct NativeTrafficOwnerCase {int input[6],result[5];unsigned count;int calls[12][3];};\n'
 text+='static constexpr NativeTrafficOwnerCase kNativeTrafficOwner[]={\n'
 for r in rows:
  assert len(r['calls'])<=12
  text+=' '+literal([r['input'],r['result'],len(r['calls']),r['calls']])+',\n'
 text+='};\nstatic constexpr unsigned kNativeTrafficGate[][4]={\n'
 text+=''.join(' '+literal(r)+',\n'for r in gates)+'};\n'
 text+='struct NativeTrafficStrategyCase {int input[4],kind;unsigned count;int calls[8];};\n'
 text+='static constexpr NativeTrafficStrategyCase kNativeTrafficStrategy[]={\n'
 text+=''.join(' '+literal([r['input'],r['kind'],len(r['calls']),r['calls']])+',\n'for r in strategies)+'};\n'
 fixture=Path(__file__).resolve().parents[1]/'tests/traffic_owner_native.inc'
 assert text.isascii()
 if args.write_fixture:fixture.write_bytes(text.encode('ascii'))
 else:assert fixture.read_text('ascii')==text,'original traffic ownership fixture differs'
 if args.out:
  relative=args.out.resolve().relative_to(Path.cwd());assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
  args.out.mkdir(exist_ok=False)
  (args.out/'oracle.json').write_text(json.dumps(dict(scope=__doc__,executable_sha256=identity,cases=rows,gates=gates,strategies=strategies),indent=2)+'\n',encoding='ascii')
 print('PASS actual traffic ownership/gate/strategy; declared IO, not complete traffic behavior')
if __name__=='__main__':main()
