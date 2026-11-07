"""Actual5A7700/5A7820 lane-change permission on explicit synthetic inputs.

Only the authenticated read-only executable is used. A synthetic raw road,
compact cell, chain flags, actor and settings are mapped separately. The full
permission function and each adjacent table edge execute without semantic
hooks. This checks categories, divider intervals and admission, not geometry,
natural steering, controller scheduling, contacts or a complete drag race.
"""
from pathlib import Path
import sys,hashlib,struct,json,itertools,argparse
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine,ACTOR,bits
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();raw=(options.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);m=Machine(image)
m.allowed=[(0x5a7700,0x5a78b0),(0x5a75c0,0x5a7688),(0x4134b0,0x4134b7),(0x582f50,0x582f57)]
route,cell,chain,environment,controller=0x1160000,0x1164000,0x1165000,0x1168000,0x1170000
m.put(0x77af34,route);m.put(cell,0x6c8f90);m.put(cell+0x5c,1,'b');m.put(cell+0x5e,0,'h');m.put(cell+0x64,chain)
m.put(ACTOR+0x24,4);m.put(ACTOR+0x2c,controller);m.put(controller,0x6b96b0);m.put(0x736174,environment)
for category in range(5):m.put(route+0x34*(category+1),1,'B')
for category in range(11,17):m.put(route+0x265+category,1,'B')
for k in range(7):m.put(chain+0xb0+k,1,'B')
tables={}
for track,start in((1206,0x6ee1e8),(1214,0x6ee2d8)):
 tables[track]=[list(struct.unpack('<iiffi',image.read(at,20)))for at in range(start,start+0xf0,20)]
rows=[]
for track in(1206,1214,1210):
 positions={bits(0.),bits(2000.)}
 if track in tables:
  for row in tables[track]:
   for edge in row[2:4]:
    v=bits(edge);positions.update((v-1,v,v+1))
 for direction,old,new,progress in itertools.product((0,1),range(11,17),range(11,17),sorted(positions)):
  m.put(environment+0x6c,track,'h');m.put(controller+0x4d4,direction);m.put(ACTOR+0x814,progress)
  m.run(0x5a7700,(old&0xffffffff,ACTOR),eax=new,ecx=cell,budget=3000)
  result=m.reg('eax')&255;steps=[];current=old
  while current!=new:
   target=current+(1 if current<new else -1)
   m.run(0x5a7820,(progress,),eax=track,edi=current,esi=target,edx=direction,budget=3000)
   steps.append(m.reg('eax')&255);current=target
  assert result==int(all(steps)),'whole5A7700 is not the adjacent fixed-table walk'
  rows.append([track,direction,old,new,progress,1,result])
# Availability is measured by actual5A75C0, including owner2's flag bypass.
for old,new,gate in itertools.product((-1,0,4,11,16,255),(-1,0,4,5,10,11,16,17),range(4)):
 for category in range(5):m.put(route+0x34*(category+1),1,'B')
 for category in range(11,17):m.put(route+0x265+category,1,'B')
 for k in range(7):m.put(chain+0xb0+k,1,'B')
 valid=0<=new<=4 or 11<=new<=16
 if valid and gate==1:m.put(route+(0x34*(new+1)if new<5 else 0x265+new),0,'B')
 if valid and gate in(2,3):m.put(chain+0xb0+(0 if new<5 else new-10),0,'B')
 m.put(ACTOR+0x24,2 if gate==3 else 4);m.put(environment+0x6c,1003,'h')
 m.run(0x5a75c0,(ACTOR,),esi=new&0xffffffff,edi=cell,budget=3000);available=m.reg('eax')&255
 m.run(0x5a7700,(old&0xffffffff,ACTOR),eax=new&0xffffffff,ecx=cell,budget=3000)
 rows.append([1003,0,old,new,bits(1000.),available,m.reg('eax')&255])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual5A7700 whole permission, inclusive interval edges, real admission.\n'
text+='struct NativeLanePermission {int track,direction,from,to;unsigned progress,available,allowed;};\n'
text+='static const NativeLanePermission kNativeLanePermission[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/traffic_lane_permission_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original lane permission fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,tables=tables,rows=rows),indent=2)+'\n',encoding='ascii')
print('PASS actual lane permission, adjacent divider walk and admission',len(rows))
