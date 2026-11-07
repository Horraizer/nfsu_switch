"""Read-only PC USA 1.4 drift scalar oracle (bounded Unicorn execution).

The base-score call stubs car/style accessors and supplies a measured slip
angle, speed, progress and yaw rate. It executes the original gates/formula.
The force call executes the original curve and 479350 using supplied heading,
velocity, yaw, drivetrain and difficulty. These are not whole-race replays.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from event0_trace import Image, chunks
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI,
                              UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_FPCW)
SHA = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'

def fbits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]

def real(m, at):
    return struct.unpack('<f', m.uc.mem_read(at, 4))[0]

def return_value(m, value, pop=0, floating=False):
    u = m.uc
    esp = u.reg_read(UC_X86_REG_ESP)
    if floating:
        # Tail-call a tiny return stub, preserving the native x87 stack.
        m.write(0x1000080, fbits(value))
        u.mem_write(0x1000000, b'\xd9\x05'+struct.pack('<I',0x1000080)+b'\xc2'+struct.pack('<H',pop))
        u.reg_write(UC_X86_REG_EIP, 0x1000000)
    else:
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, m.integer(esp))
        u.reg_write(UC_X86_REG_ESP, esp+4+pop)

def base_score(im, speed, angle, grade=0, yaw=0, progress=1, previous=0, blocked=False, phase=0):
    m = Machine(im)
    obj, car, style = 0x1001000, 0x1002000, 0x1004000
    m.write(obj+8, car);m.write(obj+0x54, fbits(previous))
    m.write(car+0x3d4, fbits(speed));m.write(car+0xd8, fbits(yaw))
    def hook(u, at, size, _):
        eax = u.reg_read(UC_X86_REG_EAX)
        if at == 0x4081e0: return_value(m,0x1005000)
        elif at == 0x424e80: return_value(m,progress,floating=True)
        elif at == 0x464ea0: return_value(m,angle,floating=True)
        elif at == 0x41e4c0: return_value(m,phase if eax == style else 0x1006000)
        elif at == 0x57f150: return_value(m,grade)
        elif at == 0x575950: return_value(m,style)
        elif at == 0x421a30: return_value(m,int(blocked))
    m.uc.hook_add(UC_HOOK_CODE,hook)
    code=0x1000100
    # push 1/60; call native (ret 4); fstp result; ret
    m.uc.mem_write(code,b'\x68'+struct.pack('<I',fbits(1/60))+b'\xe8'+struct.pack('<i',0x43e540-(code+10))+b'\xd9\x1d'+struct.pack('<I',0x1000090)+b'\xc3')
    m.run(code,registers={UC_X86_REG_ECX:obj})
    return dict(speed=speed,angle=angle,grade=grade,yaw=yaw,progress=progress,
                previous=previous,blocked=blocked,phase=phase,
                factor=real(m,0x1000090),highwater=real(m,obj+0x54))

def forces(im, angle_word, yaw, rear_fraction, difficulty):
    m=Machine(im)
    m.uc.reg_write(UC_X86_REG_FPCW,0x007f)
    obj,car,axes,body,setup,wheel0,wheel1=range(0x1001000,0x1008000,0x1000)
    for offset,pointer in [(0x58,car),(0x5c,axes),(0x20,body),(0x54,setup),(0x40,wheel0),(0x44,wheel1)]:m.write(obj+offset,pointer)
    m.write(car+0x30,fbits(20));m.write(car+0x34,0)
    m.write(car+0x374,angle_word);m.write(car+0x98,fbits(yaw))
    m.write(setup+0x1c0,fbits(rear_fraction));m.write(0x78a3fc,difficulty)
    m.write(axes+0xb8,fbits(1))
    m.run(0x68a130);m.run(0x68a1a0)
    m.run(0x479350,registers={UC_X86_REG_EDI:obj})
    if real(m,wheel0+0x180)!=real(m,wheel1+0x180):raise RuntimeError('wheel publication differs')
    return dict(angle_word=angle_word,yaw=yaw,rear_fraction=rear_fraction,difficulty=difficulty,
                torque=real(m,body+0x128),wheel_scale=real(m,wheel0+0x180))

def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
    data=(a.root/'speed.exe').read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA:raise RuntimeError('wrong original executable')
    if a.out.exists():raise RuntimeError('output exists')
    image=Image(data);image.sections=[(x,y,z) for x,y,z,_ in image.sections]
    rows=[base_score(image,v,angle,g) for g in range(4) for v in [8,12,20,30] for angle in [0,.08726646,.3,.8]]
    rows += [base_score(image,20,.5,**extra) for extra in [dict(progress=0),dict(progress=.5,previous=1),dict(blocked=True),dict(yaw=-.2),dict(yaw=.2),dict(phase=1)]]
    force_rows=[forces(image,angle,yaw,split,diff) for split in [0,.5,1] for diff in range(3) for angle,yaw in [(0,0),(4096,1),(8192,1),(16300,2),(16384,2),(57344,-1),(8192,-1)]]
    force_rows += [forces(image,angle,.1,.175,0) for angle in (8191,8192)]
    blob=(a.root/'Global/GLOBALB.BUN').read_bytes();rules=[]
    for tag,start,end in chunks(blob):
        if tag!=0x34a07:continue
        start=(start+8+15)&~15
        for i in range(11):
            b=blob[start+32*i:start+32*(i+1)]
            rules.append(dict(kind=i,language='%08X'%struct.unpack_from('<I',b)[0],flags=list(b[4:8]),
              duration=struct.unpack_from('<f',b,8)[0],timeout=struct.unpack_from('<f',b,12)[0],
              minimum=struct.unpack_from('<f',b,16)[0],limit=struct.unpack_from('<f',b,20)[0],
              rate=struct.unpack_from('<f',b,24)[0],tail=list(struct.unpack_from('<4b',b,28))))
    report=dict(executable_sha256=SHA,scope=__doc__,base=rows,forces=force_rows,retail_rules=rules)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('native complete: %d base-score cases, %d force cases, %d retail rules'%(len(rows),len(force_rows),len(rules)))
    print(json.dumps(rows[-6:],indent=2));print(json.dumps(rules,indent=2))
if __name__=='__main__':main()
