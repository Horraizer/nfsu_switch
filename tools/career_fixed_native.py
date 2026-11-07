"""PC1.4 USA authored fixed rival performance, original57F970 without semantic hooks."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from hud254_trace import PE
from physics256_native import Machine,bits,STOP,STACK
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_BLOCK
from unicorn.x86_const import UC_X86_REG_FPCW,UC_X86_REG_EIP

class FixedMachine(Machine):
    """Execute retail instructions; validate complete native basic blocks.

    No function is replaced. The until address stops before cosmetic setup.
    A block guard avoids a Python callback for every arithmetic instruction.
    """
    def __init__(self,image):
        self.uc=Uc(UC_ARCH_X86,UC_MODE_32)
        self.uc.mem_map(0x400000,0x400000);self.uc.mem_map(STOP,0x200000)
        for start,length,raw in image.sections:self.uc.mem_write(start,image.data[raw:raw+length])
        self.uc.reg_write(UC_X86_REG_FPCW,0x007F)
        self.events=[];self.allowed=[];self.visited=set()
        self.uc.hook_add(UC_HOOK_BLOCK,self.block)
    def block(self,uc,address,size,data):
        self.visited.add((address,size))
        if not any(a<=address and address+size<=b for a,b in self.allowed):
            raise RuntimeError("undeclared original block at %08X size %X"%(address,size))
    def run(self,start,args=(),end=STOP,budget=200000,**regs):
        self.reg("esp",STACK);self.put(STACK,STOP)
        for i,value in enumerate(args):self.put(STACK+4+i*4,value)
        for name,value in regs.items():self.reg(name,value)
        self.uc.emu_start(start,end,count=budget)
        if self.uc.reg_read(UC_X86_REG_EIP)!=end:raise RuntimeError("original instruction budget exhausted")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','probe','out'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    bun=(a.root/'Global/GLOBALB.BUN').read_bytes();exe=(a.root/'speed.exe').read_bytes()
    assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    m=FixedMachine(PE(exe));m.allowed=[(0x57f2b0,0x57f6f2),(0x57f970,0x57fb77),(0x575960,0x576450),
        (0x581b30,0x581d22),(0x46d1c0,0x46d384),(0x583ea0,0x583f20),(0x674898,0x674920),(0x4387e0,0x4387f6)]
    table=0x1110000;m.uc.mem_write(table,bun[1678864:1791424]);m.put(0x734588,table)
    names=[bun[1678864+i*3216:1678864+i*3216+32].split(b'\0')[0].decode('ascii') for i in range(35)]
    def ride(at,name,levels):
        m.uc.mem_write(at,bytes(0x900));m.put(at,names.index(name));m.put(at+0x410,64);m.put(at+0x420,at+0x500)
        for i,v in enumerate(levels):m.put(at+8+4*i,v)
        m.run(0x57f2b0,(0,),edi=at,budget=3000000)
    rows=[];requests=[];expected=[]
    for rival in ('CIVIC','INTEGRA','SUPRA','CELICA','RX7','SKYLINE'):
      for level in (0,1,3):
       for selector in (7,8,9):
        print("CASE",rival,level,selector,flush=True)
        player='CIVIC';x,y=0x1100000,0x1101000;ride(x,player,[level]*11);ride(y,rival,[0]*11)
        reference=m.get(x+0x428)
        m.run(0x57f970,(bits(float(selector)),level,1,bits(5.)),end=0x57fb72,eax=y,budget=30000000)
        values=[m.get(y+8+4*i) for i in range(11)]+[reference,m.get(y+0x428)]+[m.get(y+at) for at in (0x2c4,0x2c8,0x2cc)]
        requests.append(player+' '+rival+' '+str(level)+' '+str(selector));expected.append(values)
        rows.append(dict(player=player,rival=rival,level=level,selector=selector,expected=values))
    assert not m.events
    result=subprocess.run([str(a.probe.resolve()),str(a.root),'fixed-difficulty'],input='\n'.join(requests)+'\n',capture_output=True,text=True)
    (a.out/'port.log').write_text(result.stdout+result.stderr,encoding='ascii')
    actual=[list(map(int,line.split()[4:])) for line in result.stdout.splitlines() if line.startswith('ADAPTIVE ')]
    mismatches=[dict(index=i,expected=e,actual=actual[i] if i<len(actual) else None) for i,e in enumerate(expected) if i>=len(actual) or actual[i]!=e]
    good=result.returncode==0 and len(actual)==len(expected) and not mismatches
    (a.out/'result.json').write_text(json.dumps(dict(passed=good,cases=rows,mismatches=mismatches,hooks=m.events,executed_blocks=sorted(m.visited)),indent=2)+'\n',encoding='ascii')
    print('PASS' if good else 'FAIL',len(rows),'original fixed-selector cases; mismatches',len(mismatches),flush=True)
    raise SystemExit(0 if good else 1)
if __name__=='__main__':main()
