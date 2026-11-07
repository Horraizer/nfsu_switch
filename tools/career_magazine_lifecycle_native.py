"""Original4C9240 terminal Giveth-batch acknowledgement, explicit pending-list input.
Stops at the frontend transition call; no movie, GUI or car-photo behavior is claimed.
"""
from pathlib import Path
import argparse,hashlib,random,sys
sys.dont_write_bytecode=True
from world_body_trace import Image
from audio312_native import Machine,x
ap=argparse.ArgumentParser();ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
raw=(a.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';im=Image(raw);rng=random.Random(5195)
cases=[(1,[11,14]),(0,[0]),(1,[])]
for k in range(25):cases.append((rng.randrange(1<<27),rng.sample(range(27),rng.randrange(17))))
text='// Original4C9240 terminal acknowledgement; transition dispatch is an observation stop.\nstruct NativeMagazineDismiss {unsigned before,pending,after;};\nstatic constexpr NativeMagazineDismiss kNativeMagazineDismiss[]={\n'
for before,pending in cases:
 m=Machine(im,[(0x4c9240,0x4c94b0)]);m.wi(0x735e58,len(pending));m.uc.mem_write(0x6fae18,bytes(pending)+bytes(16-len(pending)))
 m.uc.mem_write(0x76777c,bytes(bool(before&(1<<j)) for j in range(27)));m.callbacks[0x4c96f0]=lambda cpu:cpu.finish()
 m.run(0x4c9240,(0x911ab364,),{x.UC_X86_REG_ECX:0x1100000})
 after=sum(1<<j for j,v in enumerate(m.uc.mem_read(0x76777c,27)) if v);assert m.ri(0x735e58)==0
 mask=sum(1<<j for j in pending);text+='    {'+str(before)+'U,'+str(mask)+'U,'+str(after)+'U},\n'
text+='};\n';target=Path('tests/career_magazine_lifecycle_native.inc')
if a.write_fixture:target.write_text(text,'ascii')
else:assert target.read_text('ascii')==text,'magazine dismissal fixture differs'
print('PASS original terminal magazine batch acknowledgement:',len(cases),'cases')
