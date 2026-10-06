"""Original queued contact episode on retail hull/handling and explicit pair poses.

This is a controlled fixture, not a live race capture. Original body, driver,
physics, list, admission, record and response instructions execute. Caller
pose/state/config, forced job scheduling, external zero-QPC and unavailable
DSOUND service, and omitted feedback tail remain explicit boundaries.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
from contact257_trace import PIN,TOOLS
from contact257_retail import inputs,prepare

TRACK_SIZE=433352
TRACK_SHA='ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA'


def episode(image,n,retail_data,decoded,offset,velocity_a,velocity_b):
 from unicorn.x86_const import UC_X86_REG_EIP
 from world_body_trace import chunks
 result={}
 m=n.fixture(image,offset,velocity_a,velocity_b)
 result['prerequisite']=prepare(m,n,retail_data)
 for head in (0x779C70,0x73D818,0x779BE0,0x73B14C):m.run(0x445720,esi=head)
 def critical_init(x):
  # Authenticated IAT697224 is InitializeCriticalSection. Single-thread service only.
  x.reg('esp',x.reg('esp')+4); x.uc.reg_write(UC_X86_REG_EIP,0x63F15E)
 m.hook(0x63F158,'external_InitializeCriticalSection_single_thread_fixture',callback=critical_init)
 for site,next_ip,label in ((0x63F198,0x63F19E,'EnterCriticalSection'),(0x63F1A8,0x63F1AE,'LeaveCriticalSection')):
  def service(x,target=next_ip):x.reg('esp',x.reg('esp')+4);x.uc.reg_write(UC_X86_REG_EIP,target)
  m.hook(site,'external_'+label+'_single_thread_fixture',callback=service)
 for site in (0x540696,0x5406E7,0x540754,0x474AA3,0x474C1E,0x474C44):
  def qpc_service(x,target=site+(6 if site in (0x474AA3,0x474C1E,0x474C44) else 2)):
   buffer=x.get(x.reg('esp'));x.put(buffer,0,'Q');x.reg('esp',x.reg('esp')+4);x.reg('eax',1);x.uc.reg_write(UC_X86_REG_EIP,target)
  m.hook(site,'external_zero_QPC_driver_marker_fixture',callback=qpc_service)
 result['phase']='original_pool_initializer466970'
 m.run(0x466970,budget=3000000)
 m.run(0x46AC40,budget=3000000);m.run(0x4655F0,budget=3000000)
 # Explicit two-body fixture capacity; no production count or cadence claim.
 m.put(0x78A41C,2);m.run(0x4764E0,budget=3000000)
 m.run(0x591D90,budget=3000000);m.run(0x53F690,budget=3000000)
 for head in (0x743C58,0x743C60,0x743C70,0x73781C):m.run(0x445720,esi=head)
 result['pools']={hex(p):m.get(p) for p in (0x736070,0x736050,0x72CBFC)}
 track_base=0x3000000;m.uc.mem_map(track_base,(len(decoded)+4095)&~4095);m.uc.mem_write(track_base,decoded)
 loaded=[]
 for tag,start,end in chunks(decoded):
  if tag in range(0x34130,0x34135):m.run(0x441ED0,(track_base+start,),budget=3000000);loaded.append(hex(tag))
 assert len(loaded)==5;result['track_loader']=dict(file='Tracks/TRACKB1003.lzc',chunks=loaded)
 for body,actor,ride in ((n.BODY_A,n.ACTOR_A,0x1120000),(n.BODY_B,n.ACTOR_B,0x1121000)):
  result['phase']='actor_state_constructor464860'
  m.run(0x464860,(ride+0x60,),eax=actor+0x40,ecx=actor+0x540,edx=actor+0x828)
  n.vector(m,actor+0x60,(0.,0.,0.) if actor==n.ACTOR_A else offset); n.matrix(m,actor+0x90)
  m.put(actor+0x3c,body)
  result['phase']='full_body_constructor469B00'
  m.run(0x469B00,(body,actor),budget=3000000)
 for body,actor,velocity in ((n.BODY_A,n.ACTOR_A,velocity_a),(n.BODY_B,n.ACTOR_B,velocity_b)):
  result['phase']='driver_constructor45DBB0'
  driver=0x1124000 if actor==n.ACTOR_A else 0x1125000
  # Original driver constructor ABI (driver,actor), no AI tick or policy claim.
  m.run(0x45DBB0,(driver,actor),budget=3000000);m.put(actor+0x30,driver)
  result['phase']='physics_factory426110_selector3'
  # Explicit no-module boundary and requested selector3; original factory owns stores.
  m.put(actor+0x28,0);m.put(actor+0x34,0);n.vector(m,actor+0x70,velocity)
  m.run(0x426110,esi=actor,ecx=3,budget=3000000)
  m.run(0x469F30,edx=body,budget=3000000)
 result['bodies']=[dict(id=m.get(b+0x18,'H'),bounds=[m.get(b+i,'f') for i in (0x24,0x28,0x2c,0x30)],shape=m.get(b+0x10),next=m.get(b+4),prev=m.get(b+8)) for b in (n.BODY_A,n.BODY_B)]
 result['phase']='registered_pair_scanner469580'
 for body in (n.BODY_A,n.BODY_B):m.run(0x469580,(body,),budget=3000000)
 node=m.get(0x779BE0);pairs=[]
 while node!=0x779BE0:
  assert len(pairs)<3;pair=node-0x250;pairs.append(dict(pair=pair,a=m.get(pair+0x258),b=m.get(pair+0x25c),record=m.get(pair+0x264)));node=m.get(node)
 result['pairs']=pairs;result['phase']='original_pair_processing468060'
 queue=0x1126000;m.run(0x445720,esi=queue)
 for entry in pairs:
  m.run(0x468060,(queue,),ecx=entry['pair'],budget=3000000)
  entry.update(record_after_processing=m.get(entry['pair']+0x264),flag14=m.get(entry['pair']+0x14,'B'),flag15=m.get(entry['pair']+0x15,'B'),scalar=m.get(entry['pair']+0x10,'f'))
 node=m.get(queue);jobs=[]
 while node!=queue:
  assert len(jobs)<3;jobs.append(dict(job=node,words=[hex(m.get(node+i*4)) for i in range(16)]));node=m.get(node)
 result['jobs']=jobs
 def actual_snapshot(x):
  states=[]
  for body,actor in ((n.BODY_A,n.ACTOR_A),(n.BODY_B,n.ACTOR_B)):
   rigid=x.get(body+0x6c)
   states.append(dict(body=hex(body),actor=hex(actor),rigid=hex(rigid),position=n.vector(x,rigid+0x20),velocity=n.vector(x,rigid+0xa0),angular_velocity=n.vector(x,rigid+0xc0),linear_momentum=n.vector(x,rigid+0x70),angular_momentum=n.vector(x,rigid+0x80),inverse_mass=x.get(rigid+0x148,'f'),inverse_inertia=[x.get(rigid+0x150+i*4,'f') for i in range(16)],state=x.get(actor+0x38),state_timer=x.get(actor+0x4cc,'f'),actor_velocity=n.vector(x,actor+0x70)))
  return states
 hits=[];applied=[];geometry=[];phases=[]
 def hit(x):
  sp=x.reg('esp');hits.append(dict(record=hex(x.get(sp+4)),point=n.vector(x,x.get(sp+8)),impulse=n.vector(x,x.get(sp+12)),tangent_speed=x.get(sp+16,'f'),bodies=actual_snapshot(x)))
 def apply(x):
  sp=x.reg('esp');event=dict(body=hex(x.reg('ecx')),return_address=hex(x.get(sp)),impulse=n.vector(x,x.get(sp+4)),arm=n.vector(x,x.get(sp+8)),bodies=actual_snapshot(x));applied.append(event)
  x.observers[x.get(sp)]=lambda y:event.update(after=actual_snapshot(y))
 m.observers[0x465830]=hit;m.observers[0x46A260]=apply
 def prepared(x):
  normal=x.reg('eax');point=x.reg('ecx');ret=x.get(x.reg('esp'))
  def after(y):
   geometry.append(dict(normal=n.vector(y,normal),point=n.vector(y,point),bodies=actual_snapshot(y)))
  x.observers[ret]=after
 m.observers[0x46B8C0]=prepared
 def phase(x):
  address=x.uc.reg_read(UC_X86_REG_EIP);event=dict(address=hex(address),before=actual_snapshot(x))
  phases.append(event)
  x.observers[x.get(x.reg('esp'))]=lambda y:event.update(after=actual_snapshot(y))
 m.observers[0x585810]=phase;m.observers[0x474A90]=phase
 result['before_response']=actual_snapshot(m)
 m.hook(0x4658B5,'declared_existing_effect_tail_skip',callback=lambda x:x.uc.reg_write(UC_X86_REG_EIP,0x465CF2))
 # Authenticated world constructor list prefix only; full world constructor remains outside fixture.
 world=m.get(0x7361F8);m.run(0x41F6FE,end=0x41F719,esi=world)
 assert m.get(world+0x10)==world+0x10 and m.get(world+0x14)==world+0x10
 result['world_record_list_prefix']={'start':hex(0x41F6FE),'end':hex(0x41F719),'world':hex(world)}
 result['phase']='original_optional_manager520460'
 # PE import DSOUND.dll ordinal1 at69702C; original6548C5 checks this service HRESULT.
 m.hook(0x6830AA,'external_DSOUND_ordinal1_no_driver_fixture',pop=12,value=0x88780078)
 m.run(0x520460,budget=3000000)
 result['optional_manager']={'pointer':hex(m.get(0x7356A8)),'flag':m.get(0x6F1DD8)}
 result['phase']='original_queued_response46B5E0'
 for job in jobs:m.run(0x46B5E0,esi=job['job'],budget=3000000)
 result['records_after_response']=[m.get(entry['pair']+0x264) for entry in pairs]
 result['records']=[dict(address=hex(record),gate=m.get(record+0x10),
  point=n.vector(m,record+0x20),accumulated=n.vector(m,record+0x50),
  magnitude_sum=m.get(record+0x60,'f'),creation_tick=m.get(record+8),
  last_hit_tick=m.get(record+0xc)) for record in result['records_after_response']]
 result['geometry']=geometry;result['phases']=phases
 result['hits']=hits;result['applied']=applied;result['after_response']=actual_snapshot(m)
 result['physics_after']=[dict(active=m.get(b+0x60),physics=m.get(b+0x68),rigid=m.get(b+0x6c)) for b in (n.BODY_A,n.BODY_B)]
 result['phase']='original_queued_response_complete'; result['success']=True
 result.update(executed=m.executed,hooks=m.events)
 return result


def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--reference-tools',type=Path,required=True)
 p.add_argument('--root',type=Path,required=True)
 p.add_argument('--out',type=Path,required=True)
 args=p.parse_args()
 for name in TOOLS:
  if (args.reference_tools/name).read_bytes()!=subprocess.check_output(['git','show',PIN+':tools/'+name]):
   raise ValueError('reference tool mismatch: '+name)
 sys.path.insert(0,str(args.reference_tools.resolve()))
 from world_body_trace import Image,manifest,jdlz,digest_file,INPUTS
 from collision256_trace import output_path
 from contact256_trace import execution_source
 import contact256_native as native
 import capstone,unicorn
 if (capstone.__version__,unicorn.__version__)!=('5.0.7','2.1.4'):
  raise ValueError('pinned dependency versions required')
 root=args.root.resolve()
 for path in [root]+list(root.rglob('*')):
  if path.is_symlink() or getattr(path.lstat(),'st_file_attributes',0)&0x400:
   raise ValueError('retail reparse point unsupported')
 out=output_path(Path.cwd().resolve(),root,args.out)
 before=manifest(root);out.mkdir(parents=True,exist_ok=False)
 (out/'before.json').write_bytes(before)
 try:
  size,sha=INPUTS['speed.exe']
  if (root/'speed.exe').stat().st_size!=size or digest_file(root/'speed.exe')!=sha:
   raise ValueError('unsupported executable')
  image=Image((root/'speed.exe').read_bytes());data=inputs(root)
  track=(root/'Tracks/TRACKB1003.lzc').read_bytes()
  if len(track)!=TRACK_SIZE or hashlib.sha256(track).hexdigest().upper()!=TRACK_SHA:
   raise ValueError('unsupported track grid input')
  decoded=jdlz(track);cases=[]
  for name,offset,va,vb in (
   ('episode_head',(3.,0.,0.),(10.,0.,0.),(-10.,0.,0.)),
   ('episode_offset',(3.,.7,0.),(10.,0.,0.),(-10.,0.,0.)),
   ('episode_glancing',(3.,.7,.2),(3.,4.,0.),(-2.,-1.,0.)),
   ('episode_separating',(3.,.7,0.),(-10.,0.,0.),(10.,0.,0.)),
   ('episode_distant',(20.,.7,0.),(10.,0.,0.),(-10.,0.,0.))):
   case=episode(image,native,data,decoded,offset,va,vb)
   if name=='episode_distant':
    if case['pairs'] or case['jobs'] or case['hits'] or case['applied']:
     raise AssertionError('distant pair incorrectly admitted')
   else:
    if len(case['pairs'])!=1 or len(case['jobs'])!=1 or len(case['records'])!=1:
     raise AssertionError('near pair/job/record missing')
    if name=='episode_separating':
     if case['hits'] or case['applied']:raise AssertionError('separating hit applied')
    elif len(case['hits'])!=1 or len(case['applied'])!=2:
     raise AssertionError('closing response or ordered pair applications missing')
   case['name']=name;case['input']=dict(offset=offset,velocity_a=va,velocity_b=vb)
   cases.append(case)
  source,blocks=execution_source(image,dict(cases=cases,cache=dict(executed={})))
  report=dict(schema='contact257-episode-v1',cases=cases,executed_blocks=blocks,
   input=dict(size=size,sha256=sha),track=dict(size=TRACK_SIZE,sha256=TRACK_SHA),
   reference_tools={name:digest_file(args.reference_tools/name) for name in TOOLS},
   worker_tool_hashes={name:digest_file(Path(__file__).with_name(name)) for name in
    ('contact257_episode.py','contact257_retail.py','contact257_trace.py')},
   runtime_capture=None,cadence=None,full_effects=False)
 finally:
  after=manifest(root);(out/'after.json').write_bytes(after)
  if before!=after:raise RuntimeError('RETAIL MANIFEST CHANGED')
 (out/'source.txt').write_text(source,encoding='ascii')
 (out/'report.json').write_text(json.dumps(report,ensure_ascii=True,allow_nan=False,indent=2)+'\n',encoding='ascii')
 print('PASS: controlled original queued contact episodes; retail manifest unchanged')
 print('UNAVAILABLE: live race scheduler, full effects and production adapter')


if __name__=='__main__':main()
