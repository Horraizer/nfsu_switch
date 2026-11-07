"""Authenticated595750 reclamation decisions on supplied one-body lists.

Original595750 and421400 execute unchanged. The list sentinel, single-human
mode and final destructor are explicit boundaries; no natural gameplay is
recorded. Only ASCII synthetic inputs and decisions are emitted.
"""
from pathlib import Path
import hashlib,struct,sys
from world_body_trace import Image
from physics256_native import Machine,STOP
raw=(Path(sys.argv[1])/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw)
def adjacent(x,delta):
    bits=struct.unpack('<I',struct.pack('<f',x))[0]
    return struct.unpack('<f',struct.pack('<I',bits+delta))[0]
def native(free,awake,exempt,distance,activation):
    body,rigid,definition,pool,world,vt=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000
    m=Machine(image);m.uc.mem_map(0,0x1000)
    m.allowed=[(0x595750,0x595b67),(0x5950e0,0x5950e7),(0x421400,0x421550)]
    m.put(0x734468,pool);m.put(pool+0x2c,32);m.put(pool+0x18,32-free)
    sentinel=0x779c70;m.put(sentinel,body+4);m.put(sentinel+4,body+4)
    m.put(body+4,sentinel);m.put(body+8,sentinel)
    m.put(body,vt);m.put(vt,STOP+0x800);m.put(vt+8,0x5950e0)
    m.put(body+0x15,1,'B');m.put(body+0x4c2,awake,'B')
    m.put(body+0x158,definition);m.put(definition+0xe4,exempt)
    m.put(body+0x168,activation);m.put(body+0x6c,rigid)
    for k,v in enumerate((distance,0.,0.)):m.put(rigid+0x20+k*4,v,'f')
    m.put(0x7361f8,world);m.put(world+0x1c,1)
    for k in range(3):m.put(world+0x280+4*k,0.,'f');m.put(world+0x290+4*k,9999999.,'f')
    m.put(0x73ad34,100000)
    m.hook(0x40cb70,'supplied list end',value=sentinel-4)
    m.hook(0x43a2e0,'supplied single-human mode',value=0)
    m.hook(STOP+0x800,'destructor observation boundary',pop=4)
    m.run(0x595750,(1,),budget=1000000)
    return any(x['hook']=='destructor observation boundary' for x in m.events)
print('// Original595750/421400, PC1.4 USA; supplied one-body pool and human anchor.')
print('struct NativePropPoolCase {unsigned free_slots,awake,exempt,activation;float distance;bool retire;};')
print('static constexpr NativePropPoolCase kNativePropPoolCases[]={')
count=0
for free in (0,3,4,9,10,32):
    for awake in (0,1):
        for exempt in (0,1):
            for activation in (0,0x7fffffff,39999,40000,40001):
                for d in (0.,)+tuple(adjacent(x,n) for x in (20.,120.,300.) for n in (-1,0,1))+(400.,):
                    value=native(free,awake,exempt,d,activation)
                    print('    {%d,%d,%d,%dU,%sF,%s},'%(free,awake,exempt,activation,d.hex(),'true' if value else 'false'))
                    count+=1
print('};')
assert count==1320