from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys
from unicorn.x86_const import UC_X86_REG_EIP
parser=argparse.ArgumentParser(description='Original selector3 two-component system versus portable code')
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--external-contact',action='store_true')
args=parser.parse_args();tree=Path.cwd();out=args.out.resolve();retail=args.root.resolve()
relative=out.relative_to(tree)
if not str(relative).isascii() or not relative.parts[0].startswith('build') or retail==out or retail in out.parents:
    parser.error('fresh ASCII workspace build output outside retail required')
out.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(tree/'tools'))
from world_body_trace import Image
from physics256_native import Machine,bits
exe=(retail/'speed.exe').read_bytes();bun=(retail/'GLOBAL/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
image=Image(exe);m=Machine(image);scratch=out
record=bun[1736752:1736752+3216];(scratch/'record.bin').write_bytes(record)
env=os.environ.copy()
cmd=['c++','-std=c++17','-O3','-fno-fast-math','-ffp-contract=off','-Wall','-Wextra','-Wpedantic',
     '-I'+str(tree/'include'),str(tree/'tools/ai_physics_probe.cpp'),str(tree/'src/ai_powertrain.cpp'),
     '-o',str(scratch/'system.exe'),'-Wl,--start-group',*[str(p) for p in sorted((tree/'build').glob('lib*.a'))],'-Wl,--end-group']
r=subprocess.run(cmd,env=env,capture_output=True,text=True)
(out/'ug0-ai-system-build.log').write_text((r.stdout+r.stderr).encode('ascii','backslashreplace').decode('ascii'),encoding='ascii');r.check_returncode()
engine,setup,body,system,vtable=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000
m.allowed=[(0x475097,0x475179),(0x475530,0x47623f),(0x584740,0x584800),(0x584800,0x584920),(0x584b10,0x584df3),
 (0x585360,0x585b37),(0x583ea0,0x58400a),(0x46aa40,0x46ac08),(0x474010,0x474041),
 (0x586720,0x58672f),(0x575940,0x575945),(0x4387e0,0x4387f6),(0x40bd40,0x40bf00),
 (0x564b10,0x564f20),(0x565060,0x565300),(0x5abdaf,0x5abf00),(0x5ac69c,0x5acd00),
 (0x674898,0x674920),(0x401cd0,0x401d70),(0x567370,0x5673d0)]
m.uc.mem_write(setup,record[0x110:]);m.put(0x6d4ac4,0x5abdb5);m.put(0x6d4acc,0x5ac6a2)
m.run(0x584800,ecx=body,eax=setup+0x110)
m.run(0x475097,end=0x475179,esi=engine,ebp=setup)
m.put(engine,0x6c847c);m.put(engine+0x18,setup);m.put(engine+0x14,body)
m.put(system,vtable);m.put(vtable+4,0x46abd0)
m.put(system+0xc,body+4);m.put(body+4,engine+4);m.put(engine+4,system+0xc)
m.put(body+0x28,.5,'f');m.put(body+0x70,20*m.get(setup+0x110,'f'),'f');m.put(body+0x138,1)
for at,value in ((0x120,2500),(0x128,.5),(0x138,1),(0x13c,1),(0x140,1)):m.put(engine+at,value,'f')
m.put(engine+0x124,2);m.put(engine+0x134,2)
for k in range(4):
 m.put(engine+0x24+4*k,60.,'f');m.put(engine+0x144+4*k,3.,'f');m.put(engine+0x64+4*k,1)
for k in range(8):m.put(engine+0x198+4*k,1000.+800*k,'f');m.put(engine+0x1bc+4*k,500.+500*k,'f')
# Supplied flat terrain external services.475BF0 still transforms actual hubs.
for site,pop in ((0x440a40,20),(0x577060,28)):
 trampoline=0x1001000+pop*8
 m.uc.mem_write(trampoline,b'\xd9\xee\xc2'+struct.pack('<H',pop));m.allowed.append((trampoline,trampoline+5))
 def flat(x,target=trampoline,normal_offset=16 if site==0x440a40 else 24):
  normal=x.get(x.reg('esp')+normal_offset)
  for j,value in enumerate((0.,0.,1.)):x.put(normal+4*j,value,'f')
  x.uc.reg_write(UC_X86_REG_EIP,target)
 m.hook(site,'supplied-flat-external-ground',callback=flat)
rows=[]
try:
 m.put(0x6f0738,0.,'f');m.run(0x46abd0,ecx=system);m.run(0x475d00,ecx=engine)
 dt=struct.unpack('<f',struct.pack('<f',1/60))[0];slice_dt=struct.unpack('<f',struct.pack('<f',dt/2))[0]
 for frame in range(24):
  if args.external_contact and frame==1:
   m.put(body+0x28,m.get(body+0x28,'f')-struct.unpack('<f',struct.pack('<f',.277))[0],'f')
   delta=struct.unpack('<f',struct.pack('<f',struct.unpack('<f',struct.pack('<f',.3))[0]*m.get(setup+0x110,'f')))[0]
   m.put(body+0x78,m.get(body+0x78,'f')-delta,'f')
   # Supplied pair separation/impulse. The completed slice leaves6F0738
   # intact:46ABD0/475A00 refresh with that interval, not constructor zero.
   m.run(0x46abd0,ecx=system);m.run(0x475d00,ecx=engine)
  for _ in range(2):
   m.put(0x6f0738,slice_dt,'f');m.run(0x46aa40,(system,bits(slice_dt)));m.run(0x475d00,ecx=engine)
   m.run(0x40bd40,edx=body+0x30,esi=body+0x30)
  values=[m.get(body+a+4*k) for a in (0x20,0x70,0x80,0xa0,0xc0,0x110,0x120) for k in range(3)]
  values += [m.get(body+0x30+16*c+4*k) for c in range(3) for k in range(3)]+[m.get(body+0x90)]
  for k in range(4):
   values += [m.get(engine+a+4*k) for a in (0x24,0x34,0x44,0x54)]
   values += [m.get(engine+a+8*k) for a in (0x74,0x78,0x94,0x98)]
   values += [m.get(engine+a+4*k) for a in (0x64,0xb8)]
  values += [m.get(engine+0x120),m.get(engine+0x124)];rows.append(values)
except Exception as ex:
 (out/'ug0-ai-system-native-incomplete.json').write_text(json.dumps(dict(failure=str(ex),completed=len(rows),visited=['%08X'%x for x in sorted(m.visited)]))+'\n',encoding='ascii');raise
r=subprocess.run([str(scratch/'system.exe'),str(scratch/'record.bin')]+(['contact'] if args.external_contact else []),input='24 '+repr(dt)+'\n',text=True,capture_output=True,env=env)
r.check_returncode();lines=r.stdout.splitlines();assert len(lines)==24
failures=[]
for frame,(expected,line) in enumerate(zip(rows,lines)):
 assert line.startswith('STATE ');actual=list(map(int,line.split()[1:]));assert len(actual)==len(expected)
 for i,(a,b) in enumerate(zip(expected,actual)):
  if a!=b and not(a in (0,0x80000000) and b in (0,0x80000000)):failures.append(dict(frame=frame,field=i,expected=a,actual=b))
report=dict(external_contact=args.external_contact,frames=len(rows),failures=failures,first=rows[0],last=rows[-1],executable_sha256=hashlib.sha256(exe).hexdigest(),globalb_sha256=hashlib.sha256(bun).hexdigest(),boundary='Original two-component list/Euler/refresh/strut/feedback/normalize with supplied flat query and fixed controls; aero/rest/contact solver omitted on both sides; optional supplied separation/impulse before frame1 retained-dt refresh. Signed zero equivalent.',hooks=m.events)
(out/'ug0-ai-system-comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
print(json.dumps(dict(frames=len(rows),failures=len(failures),first=failures[:5]),indent=2));sys.exit(bool(failures))
