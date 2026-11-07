from pathlib import Path
import argparse, subprocess, math
ap=argparse.ArgumentParser(description="Execute original NOS blur arithmetic; stub graphics submissions only.")
ap.add_argument("--retail-root",type=Path,required=True)
ap.add_argument("--probe",type=Path,required=True)
ap.add_argument("--output",type=Path,required=True)
args=ap.parse_args()
import sys,struct,json,hashlib
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
m=Machine(im);u=m.uc
u.mem_write(0x6d4ac0,bytes(u.mem_read(0x6d4be0,0x47*4)));m.write(0x6d4d00,0)
view=0x1001000;m.write(0x740500,view)
u.mem_write(view,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
# Only graphics submission is captured; math/normalization executes unchanged.
calls=[]
def hook(uc,a,size,d):
 esp=uc.reg_read(UC_X86_REG_ESP)
 if a in (0x40d370,0x40d090,0x40d6e0):
  if a==0x40d090:
   ptr=uc.reg_read(UC_X86_REG_EAX);calls.append({'pass':'radial4','offsets':list(struct.unpack('<4f',uc.mem_read(ptr,16)))})
  elif a==0x40d370:calls.append({'pass':'copy4'})
  else:calls.append({'pass':'composite','rgba':m.integer(0x100a000)})
  uc.reg_write(UC_X86_REG_EIP,m.integer(esp));uc.reg_write(UC_X86_REG_ESP,esp+4)
 if a==0x40d037:
  m.write(0x100a000,uc.reg_read(UC_X86_REG_EAX));uc.reg_write(UC_X86_REG_EIP,0x40d05d)
u.hook_add(UC_HOOK_CODE,hook)
rows=[];commands=[]
vectors=[(0,0,speed) for speed in (0,20,25,50,90,125)] + [(30,0,40),(-30,0,40),(0,30,40),(0,0,-50),(50,0,0)]
for vector in vectors:
 u.mem_write(0x73641c,struct.pack('<f',0));commands.append('reset')
 for frame in range(20):
  active=frame<8;m.write(0x73654c,int(active));u.mem_write(view+0x1e0,struct.pack('<3f',*vector))
  calls.clear();m.run(0x40cd40)
  row={'velocity':vector,'frame':frame,'nos':active,'state':struct.unpack('<f',u.mem_read(0x73641c,4))[0],'calls':list(calls)}
  rows.append(row);commands.append('frame %d %s 1'%(active,' '.join(map(str,vector))))
run=subprocess.run([str(args.probe.resolve()),'--oracle'],input='\n'.join(commands)+'\n',text=True,capture_output=True)
if run.returncode:raise RuntimeError('probe failed: '+run.stdout+run.stderr)
lines=[line for line in run.stdout.splitlines() if line!='reset']
assert len(lines)==len(rows),(len(lines),len(rows))
maximum=0.0
for row,line in zip(rows,lines):
 values=list(map(float,line.split()));assert len(values)==6
 assert struct.pack('<f',values[0])==struct.pack('<f',row['state']),row
 radial=[c for c in row['calls'] if c['pass']=='radial4']
 alpha=next((c['rgba'] for c in row['calls'] if c['pass']=='composite'),0)
 assert int(values[1])==alpha,(row,values)
 expected=radial[0]['offsets'] if radial else [0]*4
 for a,b in zip(values[2:],expected):
  error=abs(a-b);maximum=max(maximum,error)
  assert error<2e-9,(row,values,error)
 if radial:assert radial[1]['offsets']==[v*4 for v in expected]
 row['port']=values
assert hashlib.sha256((args.retail_root/'speed.exe').read_bytes()).hexdigest()==hashlib.sha256(raw).hexdigest()
args.output.parent.mkdir(parents=True,exist_ok=True)
result={'status':'PASS','cases':len(rows),'max_offset_absolute_error':maximum,'executable_sha256':hashlib.sha256(raw).hexdigest(),'scope':'Original control and normalization; D3D submissions intercepted, not original GPU pixels. No camera pullback claim.','cases_data':rows}
args.output.write_text(json.dumps(result,indent=2),encoding='ascii')
print('Original speed blur PASS: %d cases, envelope/alpha exact, max offset error %.9g'%(len(rows),maximum))
