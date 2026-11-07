"""Actual52FB60 queue ordering; supplied pending/active records and observed dispatch.
Temporary-node allocation, final list destruction and backend start/stop are boundaries.
No admission predicate or ordering branch is replaced.
"""
import sys,argparse,json,hashlib,random
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from audio312_native import Machine,x
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path);args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';im=Image(raw)
cases=[([],[]), ([(1,100),(0,5)],[(0,10),(0,20),(0,30),(0,40),(0,50)]), ([(0,100),(0,5)],[(0,10),(0,20)]), ([(1,100)],[(1,5)]*5)]
rng=random.Random(31295)
for k in range(60):cases.append(([(rng.randrange(2),rng.choice([0,5,10,10,100])) for _ in range(rng.randrange(1,8))],[(rng.randrange(2),rng.choice([0,5,10,10,100])) for _ in range(rng.randrange(1,6))]))
rows=[]
for pending,active in cases:
 m=Machine(im,[(0x52fb60,0x530000),(0x40a880,0x40a881)])
 m.uc.mem_map(0,4096);man,pool=0x1010000,0x1011000;m.wi(0x7349b4,pool);m.wi(0x7349b8,0);m.wi(0x735624,len(active));m.wi(0x6f7e94,5)
 nodes=[];objects=[]
 for i,(prio,dist) in enumerate(pending+active):
  obj=0x1020000+i*4096;m.wi(obj,0x6be520);m.wi(obj+0x344,prio);m.wf(obj+0x300,dist);objects.append(obj)
 for i in range(len(pending)):
  node=0x1100000+i*16;m.wi(node+8,objects[i]);nodes.append(node)
 sent=man+4;seq=[sent]+nodes+[sent]
 for i,node in enumerate(nodes,1):m.wi(node,seq[i+1]);m.wi(node+4,seq[i-1])
 m.wi(sent,nodes[0] if nodes else sent);m.wi(sent+4,nodes[-1] if nodes else sent);m.wi(man+12,len(pending))
 sent=man+0x24;nodes=[o+0x210 for o in objects[len(pending):]];seq=[sent]+nodes+[sent]
 for i,node in enumerate(nodes,1):m.wi(node,seq[i+1]);m.wi(node+4,seq[i-1]);m.wi(objects[len(pending)+i-1]+0x34e,1)
 m.wi(sent,nodes[0] if nodes else sent);m.wi(sent+4,nodes[-1] if nodes else sent)
 allocation=[0x1150000];calls=[]
 def alloc(cpu):
  addr=allocation[0];allocation[0]+=16;cpu.wi(pool+0x18,cpu.ri(pool+0x18)+1);cpu.return_stub(addr)
 m.callbacks[0x567160]=alloc;m.callbacks[0x421900]=lambda cpu:cpu.return_stub()
 for address,name in [(0x531350,'start'),(0x530e20,'stop')]:
  def callback(cpu,name=name):
   obj=cpu.uc.reg_read(x.UC_X86_REG_ECX);calls.append((name,objects.index(obj)));cpu.return_stub()
  m.callbacks[address]=callback
 m.run(0x52fb60,(man,));rows.append(dict(pending=pending,active=active,calls=calls))
text='// Actual original52FB60 queue ordering with dispatch observation boundaries.\nstruct NativeCollisionQueueCase {std::vector<CollisionQueueRecord> pending,active;std::vector<CollisionQueueCommand> calls;};\nstatic const NativeCollisionQueueCase kNativeCollisionQueues[]={\n'
for row in rows:
 def records(values,start):return '{'+','.join('{'+str(i+start)+'U,'+('true' if p else 'false')+','+str(d)+'.F}' for i,(p,d) in enumerate(values))+'}'
 calls='{'+','.join('{'+('true' if name=='start' else 'false')+','+str(index)+'U}' for name,index in row['calls'])+'}'
 text+='    {'+records(row['pending'],0)+','+records(row['active'],len(row['pending']))+','+calls+'},\n'
text+='};\n';target=Path('tests/collision_voice_pool_native.inc')
if args.write_fixture:target.write_text(text,'ascii')
else:assert target.read_text('ascii')==text,'collision queue native fixture differs'
if args.out:
 args.out.resolve().relative_to(Path.cwd().resolve()/'build');assert not args.out.exists();args.out.write_text(json.dumps(dict(cases=rows,boundary=__doc__),ensure_ascii=True,indent=2)+'\n','ascii')
print('PASS original full collision queue ordering:',len(rows),'cases')
