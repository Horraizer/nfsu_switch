"""Original installed-part ownership keys on actual readonly part records."""
from pathlib import Path
import sys,struct,json,hashlib,argparse
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');assert a.root.resolve() not in a.out.resolve().parents;a.out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
im=Image(raw);m=Machine(im);m.allowed=[(0x5a03d0,0x5a0445),(0x5a34d0,0x5a354c),(0x4bbd70,0x4bbd9c),(0x5a3570,0x5a360b),(0x5a3630,0x5a366d)]
tables={tag:(at+8,end) for tag,at,end in chunks(bun) if tag in (0x34604,0x34605)};first,last=tables[0x34604];attr_first,attr_last=tables[0x34605];attrs=0x10c0000;part=0x1110000;inventory=0x1100000;m.uc.mem_write(attrs,bun[attr_first:attr_last]);rows=[]
for index,at in enumerate(range(first,last,48)):
 record=bytearray(bun[at:at+48]);slot=record[16];m.run(0x5a03d0,eax=slot);cat=m.reg('eax')
 if cat>=25:continue
 for field in (20,24):
  off=struct.unpack_from('<I',record,field)[0];struct.pack_into('<I',record,field,attrs+off*8)
 m.uc.mem_write(part,bytes(record));m.uc.mem_write(inventory,bytes(4096));m.run(0x5a34d0,(inventory,),ecx=part);key=m.get(inventory+4+cat*144)
 m.run(0x5a3570,(inventory,),ecx=part);owned=bool(m.reg('eax')&255);assert owned,(index,slot,cat,key)
 rows.append((index,cat,key))
assert not m.events
(a.out/'ownership.json').write_text(json.dumps(dict(cases=rows,hooks=m.events,boundary='Original5A34D0/5A3570 on real database records and supplied empty ownership grids'),indent=2)+'\n',encoding='ascii')
lines=['// Original PC1.4 USA ownership keys; see CAREER_TUNING.md.','static constexpr unsigned kNativeTuningOwned[][3]={']+['    {'+','.join(str(v)+'U' for v in r)+'},' for r in rows]+['};'];text='\n'.join(lines)+'\n';fixture=root/'tests/career_tuning_owned_native.inc'
if a.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'ownership fixture is stale'
print('PASS original ownership keys and reinstall queries:',len(rows),'real records; no hooks')
