"""Execute original59B3E0 spacing, attribute getters and4216F0 RNG.

Zone lookup and nearby-stopped-car predicate are declared inputs. This
certifies the scalar caller and actual4420B0 point-direction admission;
not spatial lookup, the full teleporter or a natural race.
Retail files/instructions remain unchanged; the result-store stub only
reads ST0 after the call. Every generated row is compared by the C++ test.
"""
from pathlib import Path
import sys,argparse,hashlib,struct,itertools
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true')
a=ap.parse_args();raw=(a.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);rows=[]
tables=((95,115,80,95,50,70),(95,110,60,80,50,70),(130,160,85,100,60,80),(0,0,-5,5,1000,1000))
for table,selector,present,stopped,seed in itertools.product(tables,(1,2,3),(False,True),(False,True),(0,1,0x60000000,0x7fffffff,0xffffffff)):
 m=Machine(image);world,zone,actor=0x1160000,0x1162000,0x1164000
 output,stub=0x1103000,0x1104000
 m.allowed=[(0x59b3e0,0x59b48c),(0x59b670,0x59b695),(0x59b6a0,0x59b6c5),
            (0x444260,0x444265),(0x4216f0,0x421740),(stub,stub+7)]
 for j,value in enumerate(table):m.put(zone+0x7c+4*j,value,'i')
 m.put(0x78a300,selector);m.put(0x7361f8,world);m.put(world+0x2d0,seed)
 m.hook(0x424e60,'declared-actor-position',value=actor+0x60)
 m.hook(0x43db70,'declared-zone-lookup',value=zone if present else 0)
 m.hook(0x59b8d0,'declared-stopped-neighbor-predicate',pop=4,value=int(stopped))
 m.uc.mem_write(stub,b'\xd9\x1d'+struct.pack('<I',output)+b'\xc3')
 m.run(0x59b3e0,(actor,),ecx=0x1166000);m.run(stub)
 assert [e['hook']for e in m.events]==['declared-actor-position','declared-zone-lookup']+(['declared-stopped-neighbor-predicate']if present else [])
 rows.append([*table,selector,int(present),int(stopped),seed,m.get(world+0x2d0),m.get(output)])
text='// Original59B3E0/getters/RNG; spatial queries are supplied boundaries.\n'
text+='static constexpr unsigned kNativeTrafficSpacing[][12]={\n'
text+=''.join('{'+','.join(str(v&0xffffffff)+'U'for v in row)+'},\n'for row in rows)+'};\n'
# Actual4420B0 is a direct point-flag query, without any semantic hooks.
# Include every high byte and each individual low-byte bit/complement.
words=set(range(0,1<<32,1<<24))|{1<<j for j in range(32)}|{0xffffffff^(1<<j) for j in range(32)}|{0x19ffe0ff,0x07f7e0bf}
point_rows=[];m=Machine(image);point=0x1168000;m.allowed=[(0x4420b0,0x4420d0)]
for word,direction in itertools.product(sorted(words),(0,1)):
 m.put(point+0x84,word);m.run(0x4420b0,eax=point,ecx=direction)
 point_rows.append([word,direction,m.reg('eax')]);assert not m.events
text+='// Original4420B0; all four raw point-flag bytes, both travel directions.\n'
text+='static constexpr unsigned kNativeTrafficSpawnPoint[][3]={\n'
text+=''.join('{'+','.join(str(v&0xffffffff)+'U'for v in row)+'},\n'for row in point_rows)+'};\n'
# Actual599C40 endpoint margin with4420E0/432E00, no query hooks.
# The native caller has already admitted an in-range authored point.
endpoint_rows=[];m=Machine(image);route=0x116a000
m.allowed=[(0x599c40,0x599c7e),(0x4420e0,0x442100),(0x432e00,0x432e10)]
for count in(1,9,10,11,20,100,32767):
 m.put(route+0x10,count,'h')
 for point in sorted({0,count-1,9,10,count-11,count-10}):
  if not 0<=point<count:continue
  for direction in(0,1):
   m.run(0x599c40,(route+0x27c+point*0x88,route,direction))
   endpoint_rows.append([point,count,direction,m.reg('eax')&255]);assert not m.events
text+='// Original599C40 endpoint margin; caller-supplied point is in range.\n'
text+='static constexpr unsigned kNativeTrafficSpawnMargin[][4]={\n'
text+=''.join('{'+','.join(str(v)+'U'for v in row)+'},\n'for row in endpoint_rows)+'};\n'
path=Path(__file__).resolve().parents[1]/'tests/traffic_density_native.inc' 
if a.write_fixture:path.write_bytes(text.encode('ascii'))
else:assert path.read_bytes()==text.encode('ascii'),'native traffic spacing fixture differs'
print('PASS original traffic spacing/RNG, '+str(len(rows))+' boundary cases')
print('PASS original4420B0 spawn-direction admission, '+str(len(point_rows))+' boundary cases')
print('PASS original599C40 drag spawn endpoint margin, '+str(len(endpoint_rows))+' boundary cases')
