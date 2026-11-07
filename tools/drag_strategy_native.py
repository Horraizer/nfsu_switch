"""Original44DD30 policy2 over declared predicate/construction boundaries.

Executes unchanged caller instructions, not a natural race. Recovery,
wall/contact/traffic queries, allocation, reaction and constructors are
supplied witnesses. Selected kinds and query order are recorded.
"""
from pathlib import Path
import sys,argparse,hashlib,itertools
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine
from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EIP
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true')
a=ap.parse_args();raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
for kind,priority,enabled,mask in itertools.product((2,8,10,12,16,19),(-1,299,300,399,400,499,500,599,600,601),(False,True),(0,1,2,4,8,16,3,5,9,17,31)):
 m=Machine(image);m.uc.mem_map(0,0x1000) # Supplied SEH head; no exception is taken.
 controller,ctx,actor=0x1160000,0x116002c,0x1161000
 strategy,svt,cvt,marker,action,record=0x1162000,0x1163000,0x1164000,0x1165000,0x1166000,0x1167000
 calls=[];chosen=[]
 m.allowed=[(0x44dd30,0x44df6a)]
 m.put(ctx,actor);m.put(actor+0x2c,controller);m.put(controller,cvt)
 m.put(strategy,svt);m.put(svt+0x18,0x1180000)
 m.put(cvt+0x6c,0x1180010);m.put(cvt+0x70,0x1180020);m.put(cvt+0x58,0x1180030)
 m.put(ctx+0x3a8,marker);m.put(marker+0xc0,0 if mask&2 else 0xffffffff)
 def inline_obstacle_probe(uc,access,address,size,value,user):calls.append(1)
 m.uc.hook_add(UC_HOOK_MEM_READ,inline_obstacle_probe,begin=marker+0xc0,end=marker+0xc3)
 m.put(controller+0x4ec,1);m.put(record+0x34,0x123)
 m.hook(0x1180010,'declared-recovery-enabled',value=int(enabled))
 m.scalar_hook(0x1180020,'declared-recovery-speed')
 m.hook(0x1180030,'declared-controller-mode',value=1)
 def recovery(x):calls.append(0);x.ret(0,500 if mask&1 else 0xffffffff)
 def wall(x):calls.append(8);x.ret(0,300 if mask&4 else 0xffffffff)
 def contact(x):
  sp=x.reg('esp');assert x.get(sp+8)==0x41055532 and x.get(sp+12)==1
  calls.append(2);x.ret(0,record if mask&8 else 0)
 def traffic(x):
  calls.append(3);x.put(x.get(x.reg('esp')+4),0x123);x.put(x.reg('edi'),0x41200000)
  x.ret(0,400 if mask&16 else 0xffffffff)
 def constructor(selected,pop):
  def invoke(x):chosen.append(selected);x.ret(pop,action)
  return invoke
 def generic(x):
  selected=x.get(x.reg('esp')+4);assert selected in(16,19)
  chosen.append(selected);x.ret(12,action)
 for address,label,callback in ((0x451b00,'declared-recovery-query',recovery),(0x4534f0,'declared-wall-query',wall),
  (0x44ed70,'declared-contact-query',contact),(0x452b40,'declared-traffic-query',traffic),
  (0x1180000,'declared-default-constructor',constructor(8,0)),(0x4519f0,'declared-recovery-constructor',constructor(10,8)),
  (0x453090,'declared-contact-constructor',constructor(12,8)),(0x452990,'declared-traffic-constructor',constructor(2,16)),
  (0x44e2b0,'declared-generic-constructor',generic)):
  m.hook(address,label,callback=callback)
 m.hook(0x567160,'declared-allocation',value=action)
 m.hook(0x44c2e0,'declared-reaction',pop=8)
 try:m.run(0x44dd30,(ctx,kind,priority&0xffffffff),ecx=strategy)
 except Exception as error:
  raise RuntimeError(dict(input=[kind,priority,int(enabled),mask],pc=hex(m.uc.reg_read(UC_X86_REG_EIP)),registers={r:hex(m.reg(r))for r in('eax','ecx','edx','esi','edi','ebp','esp')},calls=calls))from error
 assert m.reg('eax')==action and len(chosen)==1
 rows.append([kind,priority,int(enabled),mask,chosen[0],len(calls),calls])
def literal(v):return '{'+','.join(map(literal,v))+'}'if isinstance(v,list)else str(v)
text='// Original44DD30 with explicit predicate/allocation/constructor boundaries.\n'
text+='struct NativeDragStrategyCase {int kind,priority,enabled,mask,selected;unsigned count;int calls[5];};\n'
text+='static constexpr NativeDragStrategyCase kNativeDragStrategy[]={\n'
text+=''.join(literal(row)+',\n'for row in rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/drag_strategy_native.inc'
if a.write_fixture:path.write_bytes(text.encode('ascii'))
else:assert path.read_bytes()==text.encode('ascii'),'native drag strategy fixture differs'
print('PASS original drag strategy caller, '+str(len(rows))+' declared-boundary cases')
