"""Actual457590 caller selection with declared eligibility/pair-time callbacks.

Original loop, guards, strict minimum and output writes execute.456780 and
4576B0 are explicit supplied callback results; their geometry/physics are not
certified by this caller witness. There are no input retail patches. Separate
prediction/shape/walk fixtures own pair arithmetic; this checks caller order.
"""
from pathlib import Path
import argparse,hashlib,itertools,json,sys,struct
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args();raw=(options.retail/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw);ctx,actor,controller,world,path,road,output=0x1100000,0x1110000,0x1120000,0x1130000,0x1140000,0x1150000,0x1160000
rows=[]
for values,speed,eligible,ignored,path_count,selected in itertools.product(((1.,1.,2.),(2.,1.,1.),(1.,2.,1.),(99999.,99999.,99999.),(.001,2.,3.)),(0.,.001,struct.unpack('<f',struct.pack('<I',bits(.001)+1))[0],20.,-20.),(0,1,3,7),(0,1),(0,1,20,21,255),(False,True)):
 m=Machine(image);m.allowed=[(0x457590,0x4576a7)]
 m.put(ctx,actor);m.put(ctx+4,3);m.put(actor+0x2c,controller);m.put(actor+0x3d4,speed,'f')
 m.put(controller+0x3e8,ignored);m.put(0x7361f8,world);m.put(path,path_count,'B');m.put(road,1 if selected else 0);m.put(output,bits(42.))
 for j in range(3):m.put(ctx+8+j*0x38+0x34,j+1)
 calls=[]
 def admission(box):
  address=box.get(box.reg('esp')+4);index=(address-(ctx+8))//0x38
  assert 0<=index<3;box.ret(value=int(bool(eligible&(1<<index))))
 def pair(box):
  identity=box.reg('ecx');assert 1<=identity<=3
  target=box.get(box.reg('esp')+24);box.put(target,bits(values[identity-1]));calls.append(identity)
  box.ret(pop=24,value=1)
 m.hook(0x456780,'declared eligibility result',callback=admission)
 m.hook(0x4576b0,'declared pair time/result',callback=pair)
 m.run(0x457590,(0,path,road,bits(4.),output),eax=ctx,budget=3000)
 rows.append([[bits(v)for v in values],bits(speed),eligible,ignored,path_count,int(selected),m.reg('eax'),m.get(output),len(calls),calls,m.get(world+0x2f4)])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual457590 caller with explicitly declared eligibility/pair-time callbacks.\n'
text+='struct NativePathSelection {unsigned times[3],speed,eligible,ignored,path_count,selected,target,time,call_count,calls[3],eligible_visits;};\n'
text+='static const NativePathSelection kNativePathSelection[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
fixture=Path(__file__).resolve().parents[1]/'tests/path_query_selection_native.inc';assert text.isascii()
if options.write_fixture:fixture.write_bytes(text.encode('ascii'))
else:assert fixture.read_text('ascii')==text,'original caller selection fixture differs'
if options.out:
 relative=options.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii()
 assert not options.out.exists(),'refusing stale evidence overwrite';options.out.parent.mkdir(parents=True,exist_ok=True)
 options.out.write_text(json.dumps(dict(scope=__doc__,rows=rows),indent=2)+'\n',encoding='ascii')
print('PASS original path-query caller selection',len(rows))
