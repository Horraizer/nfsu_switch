"""Actual5AAB20/5A6E10/583C80 on authenticated external raw roads.

The initialized isolated owner4/ID50 traffic fixture is explicit: physical3,
supplied compact cell coordinates, self-neighbor dimensions and signed speeds.
Direct extents cover directions0/1; full distance/arrival use the original
forward chain vtable. No semantic geometry/query hook or retail byte patch is
used. Natural admission, scheduling, actions and contacts are not certified.
"""
from pathlib import Path
import argparse,sys,runpy,capstone,struct,json
sys.dont_write_bytecode=True
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();saved_argv=sys.argv[:]
sys.argv=['tools/traffic_context_native.py',str(options.retail)]
s=runpy.run_path(str(Path(__file__).resolve().parent/'traffic_context_native.py'))
sys.argv=saved_argv
m,image,scalar=(s[k]for k in('m','image','scalar'))
from physics256_native import bits,ACTOR
from controller293_strategy import CONTROLLER
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for a,b in ((0x5aab20,0x5aaca0),(0x5a6e10,0x5a6e9b),(0x44d570,0x44d5d0),(0x5aadf0,0x5aae7a),(0x401c40,0x401c70)):
 m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
base=m.get(0x736158);count=m.get(0x73615c);assert count
chain=m.get(s['CTX']+0x3a8);cell=m.get(chain+8)
neighbors=m.get(CONTROLLER+0x3e0);assert neighbors
m.put(neighbors+4,1);m.put(neighbors+0x388,0,'b');entry=neighbors+8
assert m.get(ACTOR+0xa,'h')==50 and m.get(ACTOR+0x24)==4
extents=[];distances=[];arrivals=[];unsupported=[]
for i in range(count):
 intersection=base+i*0x170;routes=[m.get(intersection+4+k,'b')for k in range(2)]
 for route in routes+[99]:
  for direction in(0,1):
   output=scalar(0x5aab20,args=(direction,),ecx=route&0xffffffff,esi=intersection)
   extents.append([i,route,direction,bits(output)])
 for route in routes:
  raw=m.get(0x77af30+4*route)
  matches=[j for j in range(m.get(raw+0x10,'h'))if(m.get(raw+0x27c+j*0x88+0x84)>>20)&31==i]
  if not matches:unsupported.append([i,route]);continue
  point=matches[0]
  m.put(chain+4,1);m.put(chain+0xb8,0);m.put(cell+0x5c,route,'b');m.put(cell+0x5e,point,'h')
  for coordinate,along,half in((0.,0.,2.),(25.,0.,2.),(50.,12.,2.),(100.,45.5,3.25)):
   m.put(cell+0x58,coordinate,'f');m.put(entry+0x10,along,'f');m.put(entry+0xc,half,'f')
   output=scalar(0x5a6e10,args=(chain,entry))
   distances.append([i,route,point,0,bits(coordinate),bits(along),bits(half),bits(output)])
   for speed_word in (bits(-10.),bits(0.),bits(.001)-1,bits(.001),bits(.001)+1,bits(20.)):
    m.put(ACTOR+0x3d0,speed_word)
    m.run(0x583c80,(CONTROLLER,),budget=300000)
    assert m.get(intersection+0x10,'B') and m.get(intersection+0x18)==bits(output)
    arrivals.append([bits(output),speed_word,m.get(intersection+0x1c)])
assert not unsupported,'uncovered real intersection routes'
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual original raw-road extent0/1; forward cell distance/arrival.\n'
for name,fields,rows in (('Extent','int intersection,route;unsigned direction,result;',extents),('Distance','int intersection,route;unsigned point,direction,coordinate,along,half,result;',distances),('Arrival','unsigned distance,speed,result;',arrivals)):
 text+='struct NativeIntersection'+name+' {'+fields+'};\n'
 text+='static const NativeIntersection'+name+' kNativeIntersection'+name+'[]={\n'
 text+=''.join(lit(row)+',\n'for row in rows)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/traffic_intersection_geometry_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original intersection geometry fixture differs'
if options.out:
 options.out.resolve().relative_to(Path.cwd().resolve())
 assert not options.out.exists(),'refusing stale evidence overwrite'
 options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,extents=extents,distances=distances,arrivals=arrivals,unsupported=unsupported),indent=2)+'\n',encoding='ascii')
print('PASS original intersection extents',len(extents),'distances',len(distances),'arrival publications',len(arrivals))
