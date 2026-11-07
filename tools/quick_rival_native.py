"""Original57C8B0 loader,57D340 stock slots,57D860 appearance and57EA80 compatibility.
Retail files are read only and authenticated. Synthetic boundaries: a linked-list
head, a constant QueryPerformanceCounter and the observed CRT format strings.
No semantic part selection or RNG calls are hooked. Output is ASCII JSON.
"""
from pathlib import Path
import sys,struct,hashlib,json
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image,chunks
from physics256_native import Machine
from unicorn.x86_const import UC_X86_REG_EIP
import argparse
ap=argparse.ArgumentParser(description='Read-only bounded PC1.4 Quick Race appearance oracle')
ap.add_argument('retail_root',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--all-cars',action='store_true');args=ap.parse_args()
ROOT=args.retail_root
if args.output.resolve().is_relative_to(ROOT.resolve()):raise ValueError('output must be outside read-only retail root')
if args.output.exists():raise ValueError('refusing stale output; choose a fresh output path')
raw=(ROOT/'speed.exe').read_bytes();glob=(ROOT/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(glob).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections];m=Machine(im)
m.allowed=[(0x57c800,0x57f200),(0x581d30,0x5824a0),(0x53e400,0x53e480),(0x445720,0x445740),(0x40a880,0x40a890),(0x504630,0x504740),(0x567c70,0x567d00),(0x674898,0x674920),(0x417600,0x417700),(0x4f42f0,0x4f4400)]
m.uc.mem_map(0x2000000,0x400000);m.uc.mem_write(0x2000000,glob)
m.put(0x73ad40,0x73ad40);m.put(0x73ad44,0x73ad40)
for tag,at,end in chunks(glob):
 if tag in (0x34600,0x34601,0x80034602):m.run(0x57c8b0,(0x2000000+at,),budget=5000000)


def text(at):return bytes(m.uc.mem_read(at,200)).split(b'\0')[0].decode('ascii')
def formatter(m):
 sp=m.reg('esp');fmt=text(m.get(sp+4));args=[m.get(sp+8+i*4) for i in range(fmt.count('%'))]
 if fmt not in ('%s_WIDE%d_','_STYLE%02d_%2d_%2d','_BODY_%d'):raise ValueError(fmt)
 if '%s' in fmt:args[0]=text(args[0])
 out=(fmt%tuple(args)).encode('ascii');m.uc.mem_write(m.reg('esi'),out+b'\0');m.ret(value=len(out))
def va_formatter(m):
 sp=m.reg('esp');fmt=text(m.get(sp+8));va=m.get(sp+12)
 if fmt not in ('%s_DECAL_HOOD_RECT_MEDIUM','%s_DECAL_FRONT_WINDOW_WIDE_MEDIUM','%s_DECAL_REAR_WINDOW_WIDE_MEDIUM','%s_DECAL_LEFT_DOOR_RECT_MEDIUM','%s_DECAL_RIGHT_DOOR_RECT_MEDIUM','%s_DECAL_LEFT_QUARTER_RECT_MEDIUM','%s_DECAL_RIGHT_QUARTER_RECT_MEDIUM'):raise ValueError(fmt)
 args=[text(m.get(va))];out=(fmt%tuple(args)).encode('ascii');at=m.get(0x730508);m.uc.mem_write(at,out);m.put(0x730508,at+len(out));m.ret(value=len(out))
m.hook(0x565df0,'va_formatter',callback=va_formatter)
m.hook(0x565da0,'formatter',callback=formatter)
m.put(0x69717c,0x1000500);m.hook(0x1000500,'qpc',pop=4,value=1)
ride=0x1100000
base=m.get(0x73ad40);records=m.get(base+12)
results=[]
for car_id in (range(20) if args.all_cars else (0,1,4,6,11,18)):
 for level in range(4):
  for seed in (0,123,0xdeadbeef):
   m.uc.mem_write(ride,b'\0'*0x900);m.run(0x417600,edx=ride);m.put(ride,car_id)
   upgrades=[level]*11
   for i,v in enumerate(upgrades):m.put(ride+8+4*i,v)
   m.run(0x57d340,(ride,),budget=4000000);m.put(0x6f227c,seed)
   m.run(0x57d860,(ride,),budget=5000000)
   slots=[m.get(ride+0x44c+i*4) for i in range(120)]
   results.append(dict(car=text(m.get(0x734588)+car_id*0xc90),car_id=car_id,levels=upgrades,seed=seed,
       rng=m.get(0x6f227c),slots=[(v-records)//48 if v else -1 for v in slots],enabled=list(m.uc.mem_read(ride+0x6a4,120))))
ai=next(glob[a+8:b] for t,a,b in chunks(glob) if t==0x34b00)
m.uc.mem_write(0x77a400,ai[0x788:0x788+120]);m.allowed.append((0x4b4590,0x4b4610));contexts=[]
for difficulty in range(3):
 seeds={0,1,123,0x60000000,0x7fffffff,0xffffffff,0xdeadbeef}
 for threshold in struct.unpack_from('<10f',ai,0x788+difficulty*40):
  boundary=int(threshold*2147483648);seeds.update((boundary-1,boundary,boundary+1))
 for seed in sorted(s for s in seeds if 0<=s<=0xffffffff):
  m.put(0x6f227c,seed);m.run(0x4b4590,esi=difficulty)
  contexts.append([difficulty,seed,m.reg('eax'),m.get(0x6f227c)])
args.output.write_text(json.dumps(dict(visuals=results,contexts=contexts),indent=2),encoding='ascii')
print('Captured',len(results),'native cosmetic cases')
