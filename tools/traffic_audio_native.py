"""Original traffic horn/pass-by scheduler with explicit backend and spatial inputs.

Runs5251E0 including real geometric admission, RNG and fade lifecycle.
Allocator/device and positional publication are supplied boundaries. This is
not full spatial, original PCM backend, or live game equivalence.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine,bits


def prepared(im):
 m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
 env,actor,human,config,status,world,registry,parent,settings,car_type,alloc,vtable=(0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000,0x1106000,0x1107000,0x1108000,0x1110000,0x1111000,0x1112000)
 m.run(0x524970,esi=env)
 m.put(env+0x234,actor);m.put(actor+0x10,car_type);m.put(car_type,20)
 m.put(0x7356a8,parent);m.put(0x7356ac,settings);m.run(0x5364e0,(settings,))
 m.put(0x7361c4,1);m.put(0x7361bc,registry);m.put(registry+4,human)
 m.put(0x73619c,status);m.put(status+0x24,1);m.put(0x7361f8,world);m.put(world+0x28,human)
 m.put(human+0x14,config);m.put(config+4,1);m.put(human+0x3d0,40.,'f')
 m.put(human+0x60,10.,'f');m.put(human+0x90,-1.,'f');m.put(actor+0x70,10.,'f')
 # Explicit output-position input; controller/predicates/curves/RNG remain original.
 def spatial(m):
  m.put(env+0x70,18000);m.put(env+0x6c,32768);m.put(env+0xd8,1.,'f');m.ret(8)
 m.hook(0x52ea00,'supplied spatial controls',callback=spatial)
 m.put(alloc,vtable);m.put(vtable,0x1000800);m.put(vtable+4,0x1000810)
 heap=[0x1113000];dispatch=[]
 def allocator(m):
  at=heap[0];heap[0]+=0x40;m.uc.mem_write(at,bytes(24));m.ret(12,at)
 m.hook(0x1000800,'backend allocate24',callback=allocator)
 m.hook(0x1000810,'backend free',pop=4)
 def get_allocator(m):m.put(m.get(m.reg('esp')+4),alloc);m.ret()
 m.hook(0x64b510,'backend allocator interface',callback=get_allocator)
 m.hook(0x64b580,'backend allocator lock');m.hook(0x64b5a0,'backend allocator unlock')
 def play(m):
  sp=m.reg('esp');d=m.get(sp+8);dispatch.append([m.get(d+4*i) for i in range(5)]);m.ret(value=0)
 m.hook(0x64c1a0,'backend dispatch ENV_STATIC',callback=play)
 
 m.put(0x6f227c,0x12345678)

 return m,env,actor,human,car_type,settings,dispatch


def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
 args=ap.parse_args();raw=(args.retail_root/'speed.exe').read_bytes()
 assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
 im=Image(raw);rows=[];proof=[]
 for case in range(5):
  m,env,actor,human,car_type,settings,dispatch=prepared(im)
  m.put(settings+0x3e1c,30000) # Explicit pass-by category/global output input.
  m.put(car_type,22 if case==1 else 20)
  for tick in range(720):
   dt=1/60 if tick%9 else 1/120
   if case==2:
    m.put(human+0x60,3.,'f');m.put(human+0x70,-40. if tick<400 else 20.,'f')
    m.put(human+0x90,1.,'f')
   if case==3:
    m.put(human+0x60,31. if tick<200 else -10.,'f')
    m.put(human+0x3d0,24. if tick<350 else 40.,'f')
   if case==4:
    m.put(0x78a344,1,'B');m.put(human+0x60,-3.,'f');m.put(human+0x70,-100.,'f')
    m.put(human+0x90,1.,'f')
   if 300<=tick<330:m.put(0x1107000+0x84,1,'B')
   else:m.put(0x1107000+0x84,0,'B')
   m.run(0x5251e0,(bits(dt),),ecx=env)
   h=m.get(env+0x300);p=m.get(env+0x304)
   row=[case,tick,bits(dt),m.get(env+0x240),m.get(0x6f227c),int(bool(h)),int(bool(p)),m.get(env+0x2ec),m.get(env+0x2f4,'B'),m.get(env+0x31d,'B')]
   row += [m.get(h+4*i) if h else 0 for i in range(1,6)]
   row += [m.get(p+4*i) if p else 0 for i in range(1,6)]
   rows.append(row)
  proof.append(dict(case=case,dispatch=dispatch,hooks=m.events,addresses=len(m.visited)))
 fixture='// Actual5251E0 geometric gates, RNG, horns/pass-by and fade.\n// Supplied spatial publication/allocator/device; no original PCM equivalence.\n'
 fixture+='static constexpr unsigned kNativeTrafficAudio[][20]={\n'
 fixture+=''.join('    {'+','.join(map(str,r))+'},\n' for r in rows)+'};\n'
 path=Path(__file__).resolve().parents[1]/'tests/traffic_audio_native.inc'
 if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
 else:assert path.read_text(encoding='ascii')==fixture,'native traffic audio fixture differs'
 if args.out:
  args.out.mkdir(exist_ok=False)
  (args.out/'oracle.json').write_text(json.dumps(dict(rows=rows,proof=proof,scope=__doc__),indent=2)+'\n',encoding='ascii')
 print('PASS actual traffic scheduler, gates, shared RNG and lifecycle; spatial/backend explicitly supplied')


if __name__=='__main__':main()
