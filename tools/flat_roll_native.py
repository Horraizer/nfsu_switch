from pathlib import Path
import sys,json,hashlib,struct
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image
from physics256_native import bits
import contact256_native as n
import argparse,re
ap=argparse.ArgumentParser(description="Original body-only flat-turn supplied-state regression; not a world trajectory")
ap.add_argument('--root',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--fixture',type=Path,default=Path('tests/flat_roll_native.inc'))
args=ap.parse_args();root=args.root.resolve();out=args.out.resolve()
if out==root or root in out.parents or out.exists():raise ValueError('fresh non-retail output required')
check=out
while not check.exists():
 if not check.name.isascii():raise ValueError('new path component must be ASCII')
 check=check.parent
fixture=args.fixture.read_text(encoding='ascii')
words=re.findall('[+-]?0x[0-9a-f]+[.][0-9a-f]+p[+-][0-9]+F',fixture)
rows=[float.fromhex(v[:-1]) for v in words]
if len(rows)!=4*93:raise ValueError('expected four supplied42/raw42/normalized9 rows')
inputs=[rows[i:i+42] for i in range(0,len(rows),93)]
raw=[rows[i+42:i+84] for i in range(0,len(rows),93)]
normalized=[rows[i+84:i+93] for i in range(0,len(rows),93)]
def manifest():
 return {p.relative_to(root).as_posix():(p.stat().st_size,p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in root.rglob('*') if p.is_file()}
before=manifest();data=(root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest();assert sha=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
bun=(root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
im=Image(data)
cases=[]
for number,row in enumerate(inputs):
 assert len(row)==42
 m=n.fixture(im,(4,0,0),(0,0,0),(0,0,0),mass=row[0]);rigid=n.RIGID_A;props=0x1132000;system=0x1130000;table=0x1131000
 m.put(props,row[0],'f');n.matrix(m,props+0x10)
 for offset,value in zip((0,0x14,0x28),row[1:4]):m.put(props+0x10+offset,value,'f')
 m.put(props+0x50,row[4],'f');m.run(0x584800,ecx=rigid,eax=props)
 for offset,start in [(0x20,6),(0x70,9),(0x80,12),(0xa0,24),(0xc0,27),(0x110,30),(0x120,33)]:n.vector(m,rigid+offset,row[start:start+3])
 for k in range(3):n.vector(m,rigid+0x30+k*16,row[15+k*3:18+k*3])
 for offset,index in [(0x90,36),(0x1e0,37)]:m.put(rigid+offset,row[index],'f')
 for offset,index in [(0x138,38),(0x134,39),(0x130,40),(0x140,41)]:m.put(rigid+offset,int(row[index]))
 m.put(system,table);m.put(table+4,0x46abd0);m.put(system+0xc,rigid+4);m.put(rigid+4,system+0xc);m.put(rigid+8,system+0xc)
 m.put(0x6f0738,row[5],'f')
 m.run(0x46aa40,(system,bits(row[5])),budget=3000000)
 actual=row[:6]
 for offset in (0x20,0x70,0x80):actual+=n.vector(m,rigid+offset)
 for k in range(3):actual+=n.vector(m,rigid+0x30+k*16)
 for offset in (0xa0,0xc0,0x110,0x120):actual+=n.vector(m,rigid+offset)
 actual+=[m.get(rigid+0x90,'f'),m.get(rigid+0x1e0,'f')]+row[38:]
 diff=[j for j,(a,b) in enumerate(zip(actual,raw[number])) if bits(a)!=bits(b)]
 m.run(0x40bd40,edx=rigid+0x30,esi=rigid+0x30,budget=3000000)
 axes=sum([n.vector(m,rigid+0x30+k*16) for k in range(3)],[])
 assert len(actual)==len(raw[number])==42 and len(axes)==len(normalized[number])==9
 ndiff=[j for j,(a,b) in enumerate(zip(axes,normalized[number])) if bits(a)!=bits(b)]
 case={'index':number,'input':row,'expected_raw':raw[number],'original_raw':actual,'raw_differences':diff,'original_normalized_axes':axes,'normalized_differences':ndiff,'original_up':axes[8],'hooks':m.events}
 cases.append(case);print('CASE',number,'raw_differences',diff,'normalized_differences',ndiff,'original_up',axes[8],flush=True)
 assert not m.events
assert manifest()==before
report={'executable_sha256':sha,'scope':'Original constructor584800, isolated body-only46AA40/585410/585810 and40BD40 on four explicitly supplied actual port states. No world/strut/tyre production or original reachable-trajectory claim. External hooks prohibited.007F precision explicit.','retail_unchanged':True,'fixture_sha256':hashlib.sha256(args.fixture.read_bytes()).hexdigest(),'cases':cases}
out.mkdir(parents=True)
(out/'original.json').write_text(json.dumps(report,indent=2)+chr(10),encoding='ascii')
assert not any(c['raw_differences'] or c['normalized_differences'] for c in cases)
print('PASS four exact original body update/normalization cases; lower-than-.98 up witness',any(c['original_up']<.98 for c in cases))

def values(row):return '{{'+','.join(float(v).hex()+'F' for v in row)+'}}'
lines=['// Authenticated PC1.4 USA speed.exe SHA256 '+sha+'.',
       '// Original body-only46AA40/585410/585810/40BD40, precision007F, no hooks.',
       '// Four supplied port states; no original reachable-trajectory/world claim.']
for c in cases:lines.append('{'+values(c['input'])+','+values(c['original_raw'])+','+values(c['original_normalized_axes'])+'},')
(out/'flat_roll_native.inc').write_text(chr(10).join(lines)+chr(10),encoding='ascii')
