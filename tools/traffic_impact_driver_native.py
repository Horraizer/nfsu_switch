"""Verify selector2->3 traffic driver sequences against authenticated x86.

Explicit supplied actor/controller/ride/path/contact states, not live traffic.
The original45E970 and its math/history routines execute without hooks.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image,manifest
from controller293_native import fixture
from physics256_native import ACTOR,DRIVER,RIDE,PHYSICS,bits
from ai_path_vectors import literal


def f(v):return struct.unpack('<f',struct.pack('<f',v))[0]


def publication_cases(image,out,write):
 from physics256_native import Machine,STACK
 rows=[];state=0x1100000;mover=0x1102000
 for case in range(72):
  previous=[f(-1200.25 if case>=36 else 0.),f(1900.125 if case>=36 else 0.),f(2.5)]
  delta=[(.2,0,0),(.15,.07,.03),(0,0,0),(.0001,-.0002,-.0003)][case%4]
  current=[f(x+y) for x,y in zip(previous,delta)]
  heading=f([0.,1.2,-2.4,6.28][case%4]);speed=f([0.,1.e-5,12.,24.5][(case//4)%4])
  dt=f([1/60,1/120,.1][(case//16)%3]);old=(case*4003)&65535
  m=Machine(image);m.allowed=[(0x575727,0x57581d),(0x5673e0,0x56744d),(0x674898,0x67490d)]
  for k,x in enumerate(previous):m.put(STACK+0x30+k*4,x,'f')
  for k,x in enumerate(current):m.put(state+0x20+k*4,x,'f')
  m.put(state+0x374,old,'H');m.put(mover+0x14,heading,'f');m.put(mover+0x18,speed,'f')
  m.run(0x575727,(bits(dt),),end=0x57581d,edi=state,ebx=mover,ebp=STACK-4)
  assert not m.events
  expected=[m.get(state+x) for x in (0x30,0x34,0x38,0x394,0x390,0x98)]
  rows.append([previous,current,heading,speed,dt,old,expected,m.get(state+0x374,'H')])
 def lit(v):
  if isinstance(v,float):return literal(v)
  if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
  return str(v)+'u'
 text='// Original575727..57581D; supplied old/new road positions, no ground-query replacement.\nconst TrafficPublicationCase traffic_publication_cases[]={\n'+'\n'.join(lit(r)+',' for r in rows)+'\n};\n'
 generated=out/'traffic_publication_native.inc';generated.write_text(text,encoding='ascii')
 target=Path('tests/traffic_publication_native.inc')
 if write:target.write_text(text,encoding='ascii')
 else:assert target.read_bytes()==generated.read_bytes(),'publication fixture differs'
 return rows


def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
 root=a.root.resolve();out=a.out.resolve();rel=out.relative_to(Path.cwd())
 if not rel.parts[0].startswith('build') or not str(rel).isascii() or root in out.parents:
  p.error('fresh ASCII build output outside retail required')
 out.mkdir(parents=True,exist_ok=False);before=manifest(root);(out/'before.json').write_bytes(before)
 raw=(root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
 assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
 image=Image(raw);cases=[]
 try:
  for case in range(12):
   category=case%4;drive=[0.,.5,1.][case%3];yaw=f([.8,1.,1.2][case%3])
   direction=1 if case%2 else -1
   points=[(10.,direction*10.,0.),(100.,direction*60.,0.)] if case<8 else []
   heading=(case*4231)&65535
   m=fixture(image,points,speed=12.,target=20.,category=category,heading=heading)
   m.allowed.extend([(0x456b20,0x456b9b),(0x456a80,0x456b1d)])
   m.allowed.extend([(5655504, 5655750), (5655312, 5655462)])
   m.put(ACTOR+0x24,4);m.put(0x1152004,3);m.put(RIDE+0x1c0,drive,'f');m.put(RIDE+0x270,yaw,'f')
   frames=[];first=len(m.events)
   for step in range(24):
    impacted=step>=6
    speed=f([12.,10.,5.,0.,-3.,-9.][(step//4+case)%6]) if impacted else 12.
    desired=f([20.,10.,0.,1.,4.,15.][step//4])
    contact=[True]*4
    if impacted and step<12:contact=[False,False,True,True] if step<9 else [False]*4
    enabled=step!=22
    m.put(ACTOR+0x28,3 if impacted else 2)
    m.put(ACTOR+0x3d0,speed,'f');m.put(ACTOR+0x3d4,abs(speed),'f')
    m.put(ACTOR+0x70,speed,'f');m.put(ACTOR+0x74,0.,'f')
    for k,v in enumerate(contact):m.put(ACTOR+0x350+k,int(v),'B')
    for k,v in enumerate([5.,-6.,2.,7.]):m.put(ACTOR+0x374+k*8,v,'f')
    m.put(0x73ad34,40000+step*67)
    m.run(0x45e180,(bits(desired),0,0,int(enabled)),ecx=DRIVER)
    m.run(0x45e970,(bits(1/60),),ecx=DRIVER)
    history=m.get(DRIVER+0x23c)
    frames.append([speed,desired,contact,impacted,enabled,m.get(DRIVER+0x1d0,'H'),m.get(DRIVER+0x1dc),m.get(DRIVER+0x1e4),m.get(DRIVER+0x1d4,'i'),bool(m.get(DRIVER+0x218,'B')),m.get(DRIVER+0x1f4),m.get(DRIVER+0x1f8),m.get(history+0x7c)])
   assert len(m.events)==first,'unexpected external hook'
   cases.append([category,drive,yaw,heading,points,frames])
  def lit(v):
   if isinstance(v,bool):return str(v).lower()
   if isinstance(v,float):return literal(v)
   if isinstance(v,list) or isinstance(v,tuple):return '{'+','.join(map(lit,v))+'}'
   return str(v) if v<0 else str(v)+'u'
  # Fixed-size path storage retains count separately for empty-path cases.
  rows=[]
  for category,drive,yaw,heading,points,frames in cases:
   stored=points if points else [(0.,0.,0.),(0.,0.,0.)]
   rows.append(lit([category,drive,yaw,heading,len(points),stored,frames])+',')
  text='// Authenticated45E970 traffic selector transition; generated by traffic_impact_driver_native.py.\nconst TrafficImpactSequence traffic_impact_sequences[]={\n'+'\n'.join(rows)+'\n};\n'
  generated=out/'traffic_impact_driver_native.inc';generated.write_text(text,encoding='ascii')
  target=Path('tests/traffic_impact_driver_native.inc')
  if a.write_fixture:target.write_text(text,encoding='ascii')
  else:assert target.read_bytes()==generated.read_bytes(),'fixture differs; inspect original evidence before updating'
  published=publication_cases(image,out,a.write_fixture)
  report=dict(executable_sha256=sha,publication=published,cases=cases,hooks=[],fpu_control_word='007F',scope='Supplied controller4, role3, physics selector2 then3; contact/velocity/path inputs. Original math only, no live route/spawn/body integration proof.')
  (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
 finally:
  after=manifest(root);(out/'after.json').write_bytes(after);assert before==after,'retail changed'
 print('PASS original traffic selector transition:',len(cases)*24,'updates; no hooks')


if __name__=='__main__':main()
