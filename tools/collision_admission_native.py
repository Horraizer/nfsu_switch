"""Actual531FF0 admission; explicit attached records, actor identity and previous start.
No native admission branch or numeric producer is hooked.
"""
import argparse,random,hashlib
from pathlib import Path
from world_body_trace import Image
from audio312_contacts import Controller
ap=argparse.ArgumentParser();ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
raw=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';im=Image(raw);rng=random.Random(53195)
rows=[{},dict(competitor={}),dict(competitor={'direction':(-1,0,0)}),dict(direction=(0,0,1),previous=(0,0,1),previous_point=(.5,0,0),subtype=3)]
for k in range(120):
 rows.append(dict(direction=rng.choice([(1,0,0),(0,0,1),(.8,.6,0),(.707,0,.707)]),previous=rng.choice([(0,0,0),(0,0,1),(1,0,0),(0,1,0)]),previous_point=(rng.choice([0,.5,1,2]),0,0),actor=bool(rng.randrange(2)),blocked=bool(rng.randrange(2)),already_started=bool(rng.randrange(2)),subtype=rng.choice([3,4,5]),competitor=dict(active=bool(rng.randrange(2)),same_actor=bool(rng.randrange(2)),subtype=rng.choice([3,4,5]),direction=rng.choice([(1,0,0),(-1,0,0),(.799,.602,0),(.801,.599,0)]))))
def vec(v):return '{{'+','.join(format(float(x),'.9g')+('F' if '.' in format(float(x),'.9g') else '.F') for x in v)+'}}'
def record(id,actor,subtype,active,started,blocked,direction,point):return '{'+f'{id}U,{actor}U,{subtype},'+','.join('true' if v else 'false' for v in (active,started,blocked))+','+vec(direction)+','+vec(point)+'}'
text='// Actual531FF0 supplied-record admission outcomes.\nstruct NativeCollisionAdmission {CollisionAdmissionRecord current;std::array<float,3> previous,point;std::vector<CollisionAdmissionRecord> attached;bool admitted;};\nstatic const NativeCollisionAdmission kNativeCollisionAdmissions[]={\n'
for fields in rows:
 fixture=Controller(im,**fields);ok=fixture.admit();actor=int(fields.get('actor',True));subtype=fields.get('subtype',5);direction=fields.get('direction',(1,0,0));point=fields.get('point',(0,0,0));started=fields.get('already_started',False)
 current=record(1,actor,subtype,True,started,fields.get('blocked',False),direction,point);attached=[record(1,actor,subtype,True,started,False,direction,point)]
 if 'competitor' in fields:
  c=fields['competitor'];attached.append(record(2,actor if c.get('same_actor',True) else 2,c.get('subtype',5),c.get('active',True),False,False,c.get('direction',(1,0,0)),(0,0,0)))
 text+='    {'+current+','+vec(fields.get('previous',(0,1,0)))+','+vec(fields.get('previous_point',(10,0,0)))+',{'+','.join(attached)+'},'+('true' if ok else 'false')+'},\n'
text+='};\n';target=Path('tests/collision_admission_native.inc')
if a.write_fixture:target.write_text(text,'ascii')
else:assert target.read_text('ascii')==text,'collision admission native fixture differs'
print('PASS original collision admission:',len(rows),'cases')
