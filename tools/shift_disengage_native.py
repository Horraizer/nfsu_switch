"""Original high-RPM shift actors. Device allocation/category IO is declared.

The original comparison, curve, signed gain product and six bank parameters
execute unchanged. No running game, sampled PCM or control-result hooks.
"""
import argparse,hashlib,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits,STOP

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail_root',type=Path)
ap.add_argument('--write-fixture',action='store_true')
args=ap.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for side,start in enumerate([0x528d70,0x528f80]):
 for enabled in [0,1]:
  for rpm in [0.,2999.,6500.,6999.99,7000.,7000.01,8000.,10000.]:
   m=Machine(Image(raw));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
   owner,actor,table,manager,allocation=0x1120000,0x1130000,0x1140000,0x1141000,0x1150000
   m.put(0x6f1dd8,enabled);m.put(actor+4,owner);m.put(owner,table)
   m.put(owner+(0x1f1c if side==0 else 0x1d0c),rpm,'f')
   m.put(table+0x38,STOP+0x200);m.put(manager,table+0x100);m.put(table+0x100,STOP+0x220)
   categories=[];published=[]
   def category(x):
    categories.append(x.get(x.reg('esp')+8));x.ret(8)
   def allocator_context(x):
    x.put(x.get(x.reg('esp')+4),manager);x.ret()
   def allocate(x):
    assert x.get(x.reg('esp')+4)==0x1c;x.ret(12,allocation)
   def submit(x):
    sp=x.reg('esp');assert x.get(sp+4)==0x73d804
    published.append([x.get(x.get(sp+8)+i*4) for i in range(6)]);x.ret(value=0)
   m.hook(STOP+0x200,'category-output-unity',callback=category)
   m.hook(0x64b510,'allocator-context',callback=allocator_context)
   m.hook(0x64b580,'allocator-lock');m.hook(0x64b5a0,'allocator-unlock')
   m.hook(STOP+0x220,'allocate-output-event',callback=allocate)
   m.hook(0x64c1a0,'submit-output-event',callback=submit)
   m.run(start,(actor,))
   assert len(published)<=1
   assert categories==([5] if published else [])
   rows.append([side,enabled,bits(rpm),len(published),*(published[0] if published else [0]*6)])
text='// Original528D70/528F80; only declared device/category IO is supplied.\n'
text+='static constexpr unsigned kNativeShiftDisengage[][10]={\n'
text+=''.join(' {'+','.join('0x%08XU'%v for v in row)+'},\n' for row in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/shift_disengage_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original shift disengage fixture differs'
print('PASS actual high-RPM shift actor controls, rows='+str(len(rows)))
