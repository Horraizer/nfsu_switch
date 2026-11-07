"""Authentic original active-race outer damping policy on explicit binary32 states."""
from pathlib import Path
import sys,hashlib,json,struct,itertools,argparse
sys.dont_write_bytecode=True;sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import Machine,STACK
ap=argparse.ArgumentParser(description=__doc__)
for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes()
sha=hashlib.sha256(raw).hexdigest();assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);rows=[]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
for stage,gas,support,tilt,prior in itertools.product((3,4),(.0,.3,.30000004172325134,1.),(0,4),(.0,.14999999105930328,.15,-.15,.15000002086162567),(0,1)):
 m=Machine(im);m.allowed=[(0x477729,0x477775)];phy,driver,state,rigid,race=[0x1100000+i*0x1000 for i in range(5)]
 m.put(0x73619c,race);m.put(race,stage);m.put(phy+4,driver);m.put(phy+0x58,state);m.put(phy+0x20,rigid)
 m.put(driver+0x1dc,gas,'f');m.put(state+0x38c,support);m.put(state+0x58,tilt,'f');m.put(rigid+0x130,prior)
 m.put(STACK+0x18,phy)
 m.run(0x477729,end=0x477775,ecx=phy,ebx=0);assert not m.events
 rows.append([stage,bits(gas),support,bits(tilt),prior,m.get(rigid+0x130)])
a.output.write_text('// Original477729..477775, explicit outer state, no hooks. Float operands are binary32 bits.\n'+',\n'.join('{'+','.join(str(v)+'u' for v in r)+'}' for r in rows)+'\n',encoding='ascii')
a.evidence.write_text(json.dumps(dict(executable_sha256=sha,rows=rows,hooks=[],limits='Bounded actual original outer active-race damping policy on supplied actor/driver state; not a whole original race.'),indent=2)+'\n',encoding='ascii')
print('PASS original outer active-race damping',len(rows),'cases, no hooks')
