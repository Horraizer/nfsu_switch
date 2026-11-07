"""Started PCM Player destruction through original64D860/64C690.

Only type-specific PCM backend creation/status, DirectSound Stop/Release
and instance-free IO are supplied. Real652720/6584E0/651F30/6571E0 execute. The Player itself executes and changes its
mutable backend type fromFF to0. The FF case is a deliberate negative control,
not authored ownership. No audible-tail or complete backend parity is claimed.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from audio312_native import BankGraph
from hud254_trace import PE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
p=argparse.ArgumentParser(description=__doc__);p.add_argument('retail_root',type=Path);p.add_argument('--out',type=Path);args=p.parse_args()
raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=PE(raw);rows=[]
for level,name in enumerate(['SML','MED','MED','LRG']):
 bank=(args.retail_root/('Sound/CAR_2/ENG_UG_'+str(level)+'/SHIFTING_'+name+'_MB.abk')).read_bytes();module=struct.unpack_from('<I',bank,0x1c)[0]
 for negative in [False,True]:
  g=BankGraph(im,bank);m=g.m;m.allowed=[(0x400000,0x6a0000),(g.base+g.start,g.base+g.template)]
  block,pool,waves,buffer,table=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000;instance=g.base+g.template;meta=g.base+module
  created=[];status=[];stops=[];released=[];freed=[];types=[]
  assert bank[module+0x24]==1 and bank[module+0x25]==0 and bank[module+0x26]==1 and bank[module+0x27]==0
  assert bank[module+0x20:module+0x24]==bytes(4)
  m.wi(block+4,2);m.wi(0x713ee4,1);m.wi(0x713ee8,1);m.wi(0x713f48,pool)
  m.wi(pool,0x100);m.uc.mem_write(pool+0x69,b'\1')
  m.wi(0x71400c,waves);m.wi(waves+0xe8,0x6571e0);m.wi(waves+0x2c+0x3c,buffer)
  m.wi(buffer,table);m.wi(table+0x48,0x1118000);m.wi(table+8,0x1118010)
  m.uc.mem_write(meta+0x1c,struct.pack('<H',1));m.wi(meta+0x38,instance);m.wi(0x713c70,instance+8)
  def create(x):
   sp=x.uc.reg_read(UC_X86_REG_ESP);node=x.ri(sp+4);entry=x.ri(sp+8)
   assert bytes(x.uc.mem_read(node+16,1))==b'\0' and bytes(x.uc.mem_read(entry,1))==b'\0'
   created.append(node);x.return_stub(0x100)
  def playing(x):
   node=x.ri(x.uc.reg_read(UC_X86_REG_ESP)+4);assert x.ri(node+8)==0x100
   status.append(node);x.return_stub(1)
  def deletion(x):
   n=x.ri(x.uc.reg_read(UC_X86_REG_ESP)+4);x.wi(n,meta);x.wi(n+4,instance);x.wi(n+8,block)
   assert len(created)==1
   node=created[0];types.append(bytes(x.uc.mem_read(node+16,1))[0]);assert types[-1]==0
   if negative:x.uc.mem_write(node+16,b'\xff')
   del x.callbacks[0x64c690]
  def com(x,events):
   sp=x.uc.reg_read(UC_X86_REG_ESP);assert x.ri(sp+4)==buffer;events.append(buffer)
   x.uc.reg_write(UC_X86_REG_EAX,0);x.uc.reg_write(UC_X86_REG_EIP,x.ri(sp));x.uc.reg_write(UC_X86_REG_ESP,sp+8)
  def free(x):freed.append(x.ri(x.uc.reg_read(UC_X86_REG_ESP)+4));x.return_stub()
  # Unmodified original Player remains active; only its actual PCM backend is supplied.
  del m.callbacks[0x64d860]
  m.callbacks[0x64d5b0]=create;m.callbacks[0x64d730]=playing;m.callbacks[0x64c690]=deletion
  m.callbacks[0x1118000]=lambda x:com(x,stops);m.callbacks[0x1118010]=lambda x:com(x,released);m.callbacks[0x6510d0]=free
  for i,v in enumerate([1,20999,4096,0,1,0]):m.wi(instance+0x2c+i*4,v)
  m.run(g.base+g.start,(instance+24,))
  assert len(created)==len(status)==len(types)==1 and types==[0]
  assert stops==released==([]if negative else [buffer])
  assert freed==[instance] and m.ri(block+4)==1 and m.ri(meta+0x38)==0 and m.ri(0x713c70)==0
  assert bytes(m.uc.mem_read(meta+0x1c,2))==bytes(2)
  rows.append(dict(level=level,forced_ff_negative_control=negative,started_backend_type=types[0],directsound_stop_calls=len(stops),directsound_release_calls=len(released),instance_freed=True))
if args.out:
 assert not args.out.exists(),'refusing stale output'
 args.out.write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
print('PASS actual started Player through DirectSound Stop/Release and FF negative controls, cases='+str(len(rows)))
