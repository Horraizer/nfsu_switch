"""Actual529100/528D70/528F80 gain through526AA0/536C20, before device IO.

Listener0, ordinary game context and independent copied category values are
supplied inputs. No category, position, math or output result is hooked.
"""
import argparse,hashlib,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine,bits
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--write-fixture',action='store_true');args=p.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
rows=[]
for side,start in enumerate([0x529100,0x528d70,0x528f80]):
 for rpm in [3000.,6500.,6999.99,7000.,8000.,10000.]:
  for engine,shift in [(0,32767),(18000,21500),(16358,0)]:
   if side>0 and rpm<7000:continue
   m=Machine(Image(raw));m.uc.mem_map(0,0x1000);m.allowed=[(0x400000,0x6a0000)]
   owner,actor,table,tuning,settings,parent=0x1120000,0x1130000,0x1140000,0x1141000,0x1150000,0x1160000
   m.put(0x6f1dd8,1);m.put(0x7356ac,settings);m.put(0x7356a8,parent);m.put(owner,table);m.put(table+0x38,0x526aa0)
   m.put(owner+0x70,19437);m.put(owner+0x264,0)
   m.put(settings+0x130+8,engine);m.put(settings+5*0x130+8,shift)
   m.put(actor+4,owner);m.put(actor+8,tuning);m.put(actor+0xc,1,'B');m.put(tuning+8,23000,'h')
   m.put(owner+0x1f1c,rpm,'f');m.put(owner+0x1d0c,rpm,'f')
   code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(m.uc.mem_read(start,0x210)),start))
   call=next(i for i in code if i.mnemonic=='call' and '+ 0x38]'in i.op_str)
   stop=call.address+call.size
   m.run(start,(actor,1,0,4096,0)if side==0 else (actor,),end=stop)
   gain=m.get(m.reg('esp')+(0x20 if side==0 else 0xc),'i')
   # Disengage local iStack_14 is at SP+0C after its output-category call.
   rows.append([side,bits(rpm),engine,shift,gain&0xffffffff])
   assert not m.events
text='// Actual shift actors and526AA0/536C20, no substituted calls.\nstatic constexpr unsigned kNativeShiftCategory[][5]={\n'+''.join(' {'+','.join('0x%08XU'%v for v in row)+'},\n'for row in rows)+'};\n'
out=Path(__file__).resolve().parents[1]/'tests/shift_category_native.inc'
if args.write_fixture:out.write_bytes(text.encode('ascii'))
else:assert out.read_text('ascii')==text,'original shift category fixture differs'
print('PASS original shift category and positional gain, rows='+str(len(rows)))
