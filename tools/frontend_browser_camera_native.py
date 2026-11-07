"""Original starter-browser camera: bounded constructor, setters, update and view matrix.
No camera arithmetic is stubbed. Renderer/OS submission is outside the measured slice.
Linear car displacement in the samples is an explicit caller input, not a motion oracle.
"""
from pathlib import Path
import argparse
import sys,struct,json,hashlib
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from event0_trace import Image
from hud254_native import Machine
from unicorn.x86_const import *
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
m=Machine(im);cam=0x1001000;stack=0x1005000;out=0x1003000
m.uc.reg_write(UC_X86_REG_ESI,cam);m.uc.reg_write(UC_X86_REG_ESP,stack)
m.uc.emu_start(0x486c93,0x487069,count=1000)
assert m.uc.reg_read(UC_X86_REG_EIP)==0x487069
m.uc.mem_write(0x6d4ac0,bytes(m.uc.mem_read(0x6d4be0,0x47*4)));m.write(0x6d4d00,0);m.write(0x735f0c,0)
def f(at):return struct.unpack('<f',m.uc.mem_read(at,4))[0]
def update(dt,x):
 m.uc.mem_write(0x73ad38,struct.pack('<f',dt));m.uc.mem_write(cam+0x194,struct.pack('<f',x))
 m.uc.reg_write(UC_X86_REG_ECX,cam);m.uc.reg_write(UC_X86_REG_ESP,stack)
 m.uc.emu_start(0x4870c0,0x48752c,count=10000)
 assert m.uc.reg_read(UC_X86_REG_EIP)==0x48752c
 selected=m.uc.reg_read(UC_X86_REG_EAX)
 m.run(0x487680,(out,),{UC_X86_REG_EAX:selected})
 return dict(state=m.integer(cam+0x2c),elapsed=f(cam+0x18c),duration=f(cam+0x190),record=list(struct.unpack('<16f',m.uc.mem_read(selected,64))),matrix=list(struct.unpack('<16f',m.uc.mem_read(out,64))))
rows=[dict(stage='idle',frame=0,**update(0,0))]
for stage,entry in [('prepare',0x487cc0),('out',0x487da0),('in',0x487e20),('restore',0x487eb0)]:
 m.run(entry,(),{UC_X86_REG_ESI:cam,UC_X86_REG_ECX:cam})
 for i in range(91):
  t=i/60
  x=25*(t/1.5)**2 if stage=='out' else -15*(1-t/1.5) if stage=='in' else 0
  row=update(0 if i==0 else 1/60,x)
  if i in (0,15,30,45,60,90):rows.append(dict(stage=stage,frame=i,**row))
args.output.write_text(json.dumps(rows,indent=2),encoding='ascii')
for row in rows:print(row['stage'],row['frame'],row['duration'],row['record'][:5],row['matrix'][12:15])
