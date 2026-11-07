"""Bounded original5885FE compression/rate and5889EC force witnesses.
Height gap and the BSS bump coefficient are supplied inputs, not runtime defaults.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from hud254_trace import PE
from physics256_native import Machine,bits,STACK
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
ap.add_argument('--evidence',type=Path,required=True)
a=ap.parse_args()
for f in (a.output,a.evidence):
 assert not f.exists()
 assert f.resolve()!=a.root.resolve() and a.root.resolve() not in f.resolve().parents
b=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(b).hexdigest()
assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(PE(b));m.allowed=[(0x5885fe,0x5886a2),(0x5889ec,0x588a7e),(0x110f000,0x110f007)]
strut,params,wheel,wheelparams,partner,gap=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000
m.uc.mem_write(0x110f000,b'\xd9\x05'+struct.pack('<I',gap)+b'\xc3')
configs=[[5.4,35,5.4,5.8,37,.125,-.13],[5.1,30,5,5.65,30,.125,-.13]]
rows=[]
for axle,config in enumerate(configs):
 m.uc.mem_write(params,struct.pack('<7f',*config))
 for rawgap in [-1.,-.5,-.43,-.3,-.175,-.14,-.025,0.,.5,2.]:
  for dt in [0.,.0001,1/240,1/120]:
   for previous,oldrate,other,preload in [(-.1,-2.,.04,3.2),(.1,2.,-.02,2.8)]:
    for coefficient in [0.,200.]:
     radius=.3
     m.put(strut+0x14,params);m.put(strut+0x18,partner);m.put(strut+0xa0,wheel)
     m.put(strut+0x1c,previous,'f');m.put(strut+0x20,oldrate,'f')
     m.put(strut+0x88,preload,'f');m.put(partner+0x1c,other,'f')
     m.put(wheel+0x14,wheelparams);m.put(wheelparams+0x10,radius,'f')
     m.put(gap,rawgap,'f');m.put(0x6f0738,dt,'f');m.put(0x734538,coefficient,'f')
     m.run(0x110f000)
     m.run(0x5885fe,end=0x5886a2,ebx=strut,eax=wheel)
     compression,rate,overflow,contact=m.get(strut+0x1c),m.get(strut+0x20),m.get(STACK+0x18),m.get(strut+0x80)
     m.run(0x5889ec,end=0x588a7e,ebx=strut,edi=0)
     rows.append([axle]+[bits(v) for v in [rawgap,radius,previous,oldrate,dt,other,preload,coefficient]]+[compression,rate,overflow,m.get(strut+0x24),contact])
assert not m.events
lines=['// Original5885FE..5886A2 and5889EC..588A7E, x87 CW007F, no hooks.',
 '// Gap and bump coefficient are supplied; not a live default/trajectory claim.',
 'static const float strut_refresh_setup[][7]={']
for c in configs:lines.append('    {'+','.join(format(struct.unpack('<f',struct.pack('<f',v))[0],'.17g')+('F' if '.' in format(struct.unpack('<f',struct.pack('<f',v))[0],'.17g') else '.0F') for v in c)+'},')
lines+=['};','static const std::uint32_t strut_refresh_native[][14]={']
lines+=['    {'+','.join('0x%08XU'%v for v in row)+'},' for row in rows]
lines+=['};','']
a.output.write_text('\n'.join(lines),encoding='ascii')
a.evidence.write_text(json.dumps(dict(executable_sha256=sha,cases=len(rows),hooks=m.events,overflow_cap_bits=m.get(0x6f0740),scope=__doc__),indent=2)+'\n',encoding='ascii')
assert hashlib.sha256((a.root/'speed.exe').read_bytes()).hexdigest()==sha
print('PASS original strut refresh/force',len(rows),'cases; zero semantic hooks; retail unchanged')
