"""Execute authenticated PC 1.4 USA prop lift/decay instruction ranges.

Ground-cache query, shape bounds and matrix input are explicitly supplied.
This is an arithmetic witness, not a whole-game or allocator replay.
"""
from pathlib import Path
from hashlib import sha256
import json
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from contact256_native import fixture,BODY_A,CONFIG
from physics256_native import STACK

source=(Path(sys.argv[1])/'speed.exe').read_bytes()
assert sha256(source).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(source)
lifts=[]
for ground,minimum,centre,up,origin in (
        (0,-1,0,1,0),(0,-1,0,1,2),(10,-1,.25,1,9.4),
        (10,-1,.25,.8,9.4),(10,-1,.25,-.8,9.4),(0,0,.25,1,0),
        (0,-.25,.25,1,.05),(0,-.25,.25,1,.04),(0,-.25,.25,1,.06)):
    m=fixture(image,(4,0,0),(0,0,0),(0,0,0))
    m.put(BODY_A+0x158,CONFIG)
    m.put(CONFIG+0xB8,centre,'f');m.put(BODY_A+0x174,0,'f')
    for offset,value in ((0xC,ground),(0x28,minimum),(0x18,up),(0x68,origin)):
        m.put(STACK+offset,value,'f')
    m.run(0x596649,end=0x59668A,esi=BODY_A)
    lifts.append([ground,minimum,centre,up,origin,m.get(BODY_A+0x174,'f'),m.get(STACK+0x68,'f')])
decays=[]
for lift in (0,.001,.05,.3):
    for awake,activation in ((True,0),(False,0),(True,.2)):
        dt=1/60
        m=fixture(image,(4,0,0),(0,0,0),(0,0,0))
        m.put(BODY_A+0x174,lift,'f');m.put(BODY_A+0x15C,activation,'f')
        m.put(BODY_A+0x4C2,int(awake),'B');m.put(CONFIG+8,dt,'f')
        m.run(0x597614,end=0x59765D,edi=BODY_A,ebp=CONFIG)
        decays.append([lift,awake,activation,dt,m.get(BODY_A+0x174,'f')])
print(json.dumps(dict(lifts=lifts,decays=decays),indent=2))
