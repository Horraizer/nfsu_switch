"""Bounded original drift combo oracle; explicit style/timer accessor inputs."""
import argparse,hashlib,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from hud254_trace import PE
from hud254_native import Machine
from drift_native import SHA,return_value
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP

def combo(im,grade,mult,speed,phase,points,blocked,timer,then,now):
    m=Machine(im);obj=0x1001000;style=0x1002000
    targets=((350,1400,4200,11200),(300,1200,3600,9600),(250,1000,3000,8000),(200,800,2400,6400))
    target=targets[grade][mult-1] if mult<5 else 0
    for off,v in [(0xc,grade),(0x18,mult),(0x1c,target),(0x20,then if timer else 0),(0x24,int(blocked))]:m.write(obj+off,v)
    m.write(0x73ad3c,now)
    def hook(u,at,size,_):
        if at==0x575950:return_value(m,style)
        elif at==0x424e10:return_value(m,speed,floating=True)
        elif at==0x41e4c0:return_value(m,phase)
        elif at==0x432c80:return_value(m,points,floating=True)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.run(0x43e790,(0,),{UC_X86_REG_ECX:obj})
    flags=list(m.uc.mem_read(obj+0x12,2))
    return [grade,mult,speed,phase,points,int(blocked),int(timer),then if timer else 0,now,target,
            m.uc.reg_read(UC_X86_REG_EAX),m.integer(obj+0x1c),*flags,int(m.integer(obj+0x20)!=0),m.integer(obj+0x20)]

def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
    b=(a.root/'speed.exe').read_bytes()
    if hashlib.sha256(b).hexdigest()!=SHA or a.out.exists():raise RuntimeError('identity or fresh-output refusal')
    im=PE(b);rows=[]
    for g in range(4):
        for mult in range(1,6):
            for speed,phase,points,blocked,timer,then,now in [
                (30,0,100000,False,False,0,10000),(30,2,100000,False,False,0,10000),
                (3,0,100000,False,False,0,10000),(30,0,100000,True,False,0,10000),
                (3,0,100000,False,True,4000,10000),(3,0,100000,False,True,3999,10000),
                (30,0,100000,False,True,4000,10000),(30,0,100000,False,True,3999,10000)]:
                rows.append(combo(im,g,mult,speed,phase,points,blocked,timer,then,now))
    # Strict original score promotion and binary32 speed boundaries.
    targets=((350,1400,4200,11200),(300,1200,3600,9600),
             (250,1000,3000,8000),(200,800,2400,6400))
    for g in range(4):
        for mult in range(1,5):
            target=targets[g][mult-1]
            for points in (target-1,target,target+1):
                rows.append(combo(im,g,mult,30,0,points,False,False,0,10000))
    for mult in range(1,6):
        threshold=(0,8.333333969116211,11.111111640930176,
                   13.88888931274414,16.666667938232422)[mult-1]+1.388888955116272
        bits=struct.unpack('<I',struct.pack('<f',threshold))[0]
        for word in (bits-1,bits,bits+1):
            speed=struct.unpack('<f',struct.pack('<I',word))[0]
            for timer in (False,True):
                rows.append(combo(im,0,mult,speed,0,100000,False,timer,4000,10000))
    a.out.write_text('# Original 43E790; style and timer accessors supplied. PE SHA256 '+SHA+'\n'+'\n'.join(' '.join(map(str,r)) for r in rows)+'\n',encoding='ascii')
    print('PASS native combo completed',len(rows),'cases')
if __name__=='__main__':main()
