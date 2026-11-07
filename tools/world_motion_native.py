from pathlib import Path
from hashlib import sha256
import json
import struct
import math
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from world_body_trace import Image, chunks
from contact256_native import ContactMachine
from physics256_native import bits


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def word(data, at):
    return struct.unpack_from('<H', data, at)[0]


def name_hash(name):
    out = 0xffffffff
    for byte in name.encode('ascii'):
        out = (out * 33 + byte) & 0xffffffff
    return out


def clips(source):
    out = []
    for tag, start, end in chunks(source):
        if tag != 0xe34010:
            continue
        elf = source[(start + 23) & ~15:end]
        if elf[:4] != b'\x7fELF':
            continue
        sections = u32(elf, 32)
        data = None
        symbols = {}
        for i in range(word(elf, 48)):
            at = sections + i * 40
            kind, offset, size = u32(elf, at + 4), u32(elf, at + 16), u32(elf, at + 20)
            if kind == 1 and u32(elf, at + 8) & 2:
                data = elf[offset:offset + size]
            if kind == 2:
                strings = u32(elf, sections + 40 * u32(elf, at + 24) + 16)
                for row in range(offset, offset + size, 16):
                    p = strings + u32(elf, row)
                    symbols[elf[p:elf.index(b'\0', p)].decode('ascii')] = u32(elf, row + 4)
        if data is None or '__AnimationBank:::bank' not in symbols:
            continue
        bank = symbols['__AnimationBank:::bank']
        for i in range(u32(data, bank + 4)):
            p = u32(data, u32(data, bank + 16) + i * 4)
            name = data[p:data.index(b'\0', p)].decode('ascii')
            if name_hash(name) not in (0x9d2624dd, 0x9d2624da, 0xa81847b9, 0xa81847b6):
                continue
            at = u32(data, u32(data, bank + 12) + 4 * i)
            out.append((name, data, u32(data, at + 12)))
    return out


def machine(image):
    m = ContactMachine(image)
    pe = u32(image.data, 0x3c)
    opt = word(image.data, pe + 20)
    for i in range(word(image.data, pe + 6)):
        at = pe + 24 + opt + 40 * i
        va, size = u32(image.data, at + 12), u32(image.data, at + 16)
        if u32(image.data, at + 36) & 0x20000000:
            m.allowed.append((0x400000 + va, 0x400000 + va + size))
    m.run(0x5b065b, (0,))
    return m


import argparse
ap=argparse.ArgumentParser(description="Execute authenticated original world animation and kind4 response functions")
ap.add_argument('retail',type=Path)
ap.add_argument('output',type=Path)
a=ap.parse_args()
root=a.retail
try:
    a.output.resolve().relative_to(root.resolve())
except ValueError:
    pass
else:
    raise SystemExit('retail output is forbidden')
if not a.output.name.isascii():raise SystemExit('output filename must be ASCII')
if a.output.exists():raise SystemExit('refusing to overwrite native evidence')
exe = (root / 'speed.exe').read_bytes()
assert sha256(exe).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image = Image(exe)
result = {'clock': [], 'samples': []}
base, ctx, pose, heap = 0x1120000, 0x1100000, 0x1101000, 0x1180000
m = machine(image)
for flags in (0x10, 0x20, 0x40, 0xa0, 0x50, 0x60):
    for rate in (-.5, .32, .5, 1.):
        for phase in (-.01, 0., 98.99, 99.):
            for dt in (0., 1 / 60, .1):
                for at, value, fmt in ((0x34, flags, 'I'), (0x5c, rate, 'f'), (0x60, 99., 'f'),
                                        (0x64, phase, 'f'), (0x68, .2, 'f'), (0x6c, .1, 'f')):
                    m.put(ctx + at, value, fmt)
                m.run(0x47b290, (bits(dt),), ecx=ctx)
                result['clock'].append(dict(flags=flags, rate=rate, phase=phase, dt=dt,
                    after=m.get(ctx + 0x64, 'f'), after_rate=m.get(ctx + 0x5c, 'f'),
                    hold=m.get(ctx + 0x6c, 'f'), status=m.reg('eax')))

for name, data, at in clips((root / 'Global/INGAMEB.BUN').read_bytes()):
    m = machine(image)
    m.uc.mem_write(base, data)
    channel = base + at
    kind = word(data, at)
    assert kind in (17, 20)
    for offset in ((8, 12) if kind == 17 else (4, 8)):
        value = u32(data, at + offset)
        if value:
            m.put(channel + offset, value + base)
    m.put(ctx + 12, channel)
    m.put(ctx + 16, 0xffffffff)
    m.put(ctx + 28, 0xffffffff)
    if kind == 17:
        count, frame_count, shift = data[at + 6], word(data, at + 4), data[at + 16]
        block = 1 << shift
        block_size = ((block + 1) * count * 3 + 1) & ~1
        tail = channel + 18 + count * 12 + (frame_count // block) * block_size
        if frame_count % block:
            tail += (frame_count % block + 1) * count * 3
        m.put(ctx + 16, channel + 18)
        m.put(ctx + 20, channel + 18 + count * 12)
        m.put(ctx + 24, block_size)
        m.put(ctx + 32, heap)
        m.put(ctx + 36, heap)
        m.put(ctx + 40, tail)
        m.put(ctx + 44, (tail + data[at + 7] + 1) & ~1)
    else:
        # Declare only the allocator boundary; all scale/header arithmetic runs original code.
        alloc_object, alloc_vtable = heap + 0x1000, heap + 0x1100
        m.put(0x70d364, alloc_object)
        m.put(alloc_object, alloc_vtable)
        m.put(alloc_vtable + 20, 0x67121a)
        allocation = [heap + 0x2000]
        def allocate(current):
            where = allocation[0]
            allocation[0] += 0x1000
            current.ret(4, where)
        m.hook(0x67121a, 'declared evaluator allocator', callback=allocate)
        m.run(0x62d260, ecx=ctx)
        frame_count = word(data, at + 12)
    for frame in (0., .16, .5, 1., 6., 30., 50.7, float(frame_count - 1)):
        for i, value in enumerate((1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0) * 2):
            m.put(pose + i * 4, value, 'f')
        try:
            m.run(0x6315f0 if kind == 17 else 0x62df90, (bits(frame), pose, 0), ecx=ctx, budget=1000000)
        except Exception:
            print(name, frame, hex(m.uc.reg_read(26)), {r:hex(m.reg(r)) for r in ('eax','ebx','ecx','edx','esi','edi')}, [hex(m.get(ctx+i*4)) for i in range(12)])
            raise
        sample = [m.get(pose + 48 + i * 4, 'f') for i in range(12)]
        matrix = pose + 0x100
        m.run(0x627d80, tuple(bits(sample[k]) for k in (0,1,2,4,5,6,7,8,9,10)), ecx=matrix)
        result['samples'].append(dict(name=name, hash=name_hash(name), frame=frame,
                                      pose=sample, matrix=[m.get(matrix+i*4,'f') for i in range(16)]))

import contact256_native as n
result['kinematic'] = []
for remaining in (0., .004, .008, .016):
    m = n.fixture(image,(0,0,0),(11.,-3.,2.),(0,0,0))
    world,node=0x1150000,0x1151000
    m.put(n.BODY_A,0x6b8d88);m.put(n.BODY_A+0x15,4,'B');m.put(n.BODY_A+0x60,0)
    m.put(n.BODY_A+0x140,world);m.put(world,0x6ba65c);m.put(world+0x1c,node)
    m.put(n.BODY_A+0x70,.016,'f');n.vector(m,n.BODY_A+0xc0,(1.,2.,3.))
    n.vector(m,n.BODY_A+0xd0,(3.,-1.,7.));n.matrix(m,n.BODY_A+0x80)
    n.matrix(m,node+0x10);n.vector(m,node+0x40,(2.,4.,5.));n.vector(m,node+0x90,(11.,-3.,2.))
    for i,v in enumerate((0.,1.,0.,0.,-1.,0.,0.,0.,0.,0.,1.,0.)):m.put(node+0x10+4*i,v,'f')
    pos,axes,velocity=0x1140000,0x1140100,0x1140200
    m.run(0x468fe0,(pos,axes,bits(remaining)),ecx=n.BODY_A)
    m.run(0x4692f0,(bits(remaining),),esi=n.BODY_A,edi=velocity)
    result['kinematic'].append(dict(remaining=remaining,position=n.vector(m,pos),
        axes=[[m.get(axes+16*i+4*j,'f') for j in range(3)] for i in range(3)],velocity=n.vector(m,velocity)))

result['moving_contact']=[]
from unicorn.x86_const import UC_X86_REG_EIP
for speed in (5.,20.,50.):
 for side in (0.,.7):
  for world_first in (False,True):
   for velocity in ((-10.,0.,0.),(10.,0.,0.),(0.,10.,0.)):
    offset=(1.9,side,.2)
    m=n.fixture(image,offset,(speed,0,0),(0,0,0),state=0)
    obj,node,ride=n.CONFIG+0x3000,n.CONFIG+0x4000,n.CONFIG+0x6000
    wb,cb,cr,ca=(n.BODY_A,n.BODY_B,n.RIGID_B,n.ACTOR_B) if world_first else (n.BODY_B,n.BODY_A,n.RIGID_A,n.ACTOR_A)
    for rigid,actor,pos,vel in ((n.RIGID_A,n.ACTOR_A,offset if world_first else (0,0,0),velocity if world_first else (speed,0,0)),
                               (n.RIGID_B,n.ACTOR_B,(0,0,0) if world_first else offset,(speed,0,0) if world_first else velocity)):
     n.vector(m,rigid+0x20,pos);n.vector(m,actor+0x60,pos);n.vector(m,rigid+0xa0,vel);n.vector(m,actor+0x70,vel);n.vector(m,rigid+0x70,[2*v for v in vel])
    m.put(wb,0x6b8d88);m.put(wb+0x15,4,'B');m.put(wb+0x60,0);m.put(wb+0x140,obj)
    m.put(obj,0x6ba65c);m.put(obj+0x1c,node)
    n.matrix(m,node+0x10);n.matrix(m,node+0x50);n.vector(m,node+0x40,offset)
    n.vector(m,node+0x90,velocity)
    m.put(ca+0x20,ride);m.put(ride+0x14,-.7,'f');m.put(ride+0x44,.7,'f')
    n.vector(m,n.PAIR+0x20,(.95,side*.5,.1));n.vector(m,n.PAIR+0x30,(.95,side*.5,.1))
    m.run(0x46b8c0,edi=n.JOB,eax=n.NORMAL,ecx=n.POINT)
    m.hook(0x4658b5,'declared audiovisual tail omitted after465D00',callback=lambda x:x.uc.reg_write(UC_X86_REG_EIP,0x465cf2))
    m.run(0x46bae0,(n.JOB,),budget=3000000)
    result['moving_contact'].append(dict(speed=speed,side=side,world_first=world_first,velocity=velocity,
      momentum=n.vector(m,cr+0x70),angular=n.vector(m,cr+0x80),state=m.get(ca+0x38),
      gate=m.get(n.RECORD+0x10),magnitude=m.get(n.RECORD+0x60,'f'),seed=m.get(n.CONFIG+0x12d0)))
from unicorn.x86_const import UC_X86_REG_FP0,UC_X86_REG_FPSW
result['arrival']=[]
for distance in (-10.,0.,20.,100.,300.):
 for speed in (0.,.001,10.,50.):
  for acceleration in (-2.,-.001,0.,.001,2.):
   m=machine(image);actor=0x1160000
   m.put(actor+0x3d4,speed,'f');n.vector(m,actor+0x80,(acceleration,0.,0.));n.vector(m,actor+0x90,(1.,0.,0.))
   m.run(0x4570d0,(bits(distance),),eax=actor)
   top=(m.uc.reg_read(UC_X86_REG_FPSW)>>11)&7
   mantissa,exponent=m.uc.reg_read(UC_X86_REG_FP0+top)
   value=math.ldexp(mantissa/2**63,(exponent&0x7fff)-16383)*(-1 if exponent&0x8000 else 1)
   value=struct.unpack('<f',struct.pack('<f',value))[0]
   result['arrival'].append(dict(distance=distance,speed=speed,acceleration=acceleration,arrival=value))
a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='ascii')
print('Original clock cases:',len(result['clock']),'clip samples:',len(result['samples']),
      'kinematic samples:',len(result['kinematic']),'moving responses:',len(result['moving_contact']),
      'arrival cases:',len(result['arrival']))
