"""Original4E3F90 special vinyl style gate with supplied empty ownership.
The slice stops after original4AA9D0; shop construction and award UI are outside it.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');a.out.mkdir(exist_ok=False,parents=True)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
tables={tag:(at+8,end) for tag,at,end in chunks(bun) if tag in (0x34604,0x34a08)}
first,last=tables[0x34a08];style=[list(struct.unpack_from('<8I',bun,at)) for at in range((first+15)&~15,last-31,32)]
rewards={r[2]:r[0] for r in style if r[1]&65535==2};first,last=tables[0x34604]
records=[bun[at:at+48] for at in range(first,last,48) if bun[at+16]==31 and bun[at+18] in (15,16) and struct.unpack_from('<I',bun,at+8)[0] in rewards]
# Include both earned level4 CUSTOM entries and normal manufacturer entries.
records=[next(r for r in records if r[18]==15 and r[17]==4),next(r for r in records if r[18]==16)]
m=Machine(Image(raw));m.allowed=[(0x4e3f90,0x4e4044),(0x4aa970,0x4aaab4)]
source,part,career=0x10a0000,0x1110000,0x7588c4
m.uc.mem_write(source,b''.join(struct.pack('<8I',*r) for r in style));m.put(0x7343f8,source);m.put(0x7343fc,len(style));cases=[]
for record in records:
 key=struct.unpack_from('<I',record,8)[0];threshold=rewards[key]
 for points in (0,threshold-1,threshold,threshold+1):
  m.uc.mem_write(career,bytes(0x9000));m.run(0x4aa970,(0,points),ebx=career+0x214)
  m.uc.mem_write(part,record);m.run(0x4e3f90,(0,),end=0x4e4043,esi=part,ebx=0)
  result=int(bool(m.reg('eax')&255));assert result==int(points>=threshold)
  cases.append([record[18],record[17],key,points,result])
assert not m.events
fixture='// Original4E3F90 special types15/16 and4AA970/4AA9D0 flags, no call hooks.\nstatic constexpr unsigned kNativeSpecialVinyl[][5]={\n'+'\n'.join('    {'+','.join(str(v)+'U' for v in row)+'},' for row in cases)+'\n};\n'
path=root/'tests/career_special_vinyl_native.inc'
if a.write_fixture:path.write_text(fixture,encoding='ascii')
else:assert path.read_text(encoding='ascii')==fixture
(a.out/'evidence.json').write_text(json.dumps({'scope':__doc__,'cases':cases,'hooks':m.events},indent=2)+'\n',encoding='ascii')
print('PASS original special Unique/manufacturer vinyl style gates:',len(cases),'cases, no hooks')
