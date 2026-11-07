"""Original PC1.4 USA underbody neon color controller, supplied render state."""
from pathlib import Path
import sys,struct,json,hashlib,argparse
sys.dont_write_bytecode=True;root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image,chunks
from physics256_native import Machine,STACK
from unicorn import x86_const as x
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--write-fixture',action='store_true');a=p.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');assert a.root.resolve() not in a.out.resolve().parents;a.out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();bun=(a.root/'Global/GLOBALB.BUN').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7';assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
im=Image(raw);m=Machine(im);m.allowed=[(0x56f061,0x56f1f2),(0x564b10,0x564bc4),(0x674898,0x674910)];body=0x1100000
children={t:(at+8,end) for t,at,end in chunks(bun) if t in (0x34604,0x34605)};first,last=children[0x34604];attrs,end=children[0x34605]
def hash_name(s):
 h=0xffffffff
 for c in s.encode('ascii'):h=(h*33+c)&0xffffffff
 return h
rows=[];lines=['// Original56F061 color controller on authored neon colors and supplied strength.', 'struct NativeCareerNeon {CarNeonSettings settings;unsigned ticks;float strength;unsigned color;};','static constexpr NativeCareerNeon kNativeCareerNeon[]={']
for at in range(first,last,48):
 r=bun[at:at+48]
 if r[16]!=116:continue
 index=struct.unpack_from('<I',r,20)[0];count=struct.unpack_from('<h',r,28)[0];values=dict(struct.unpack_from('<II',bun,attrs+(index+k)*8) for k in range(count));base=[values[hash_name(s)] for s in ('RED','GREEN','BLUE','ALPHA')];delta=[values[hash_name(s+'2')]-values[hash_name(s)] for s in ('RED','GREEN','BLUE','ALPHA')]
 for ticks in (0,1,30,300,1000,5000,20000,64127,300999):
  for strength in (0.0,0.42,1.0):
   m.uc.reg_write(x.UC_X86_REG_FPTAG,0xffff);m.uc.reg_write(x.UC_X86_REG_FPSW,0);m.put(0x73ad34,ticks);m.put(0x77a920,0);m.put(STACK+0x58,strength,'f');m.uc.mem_write(body+0x4c0,struct.pack('<8h',*(base+delta)));m.run(0x56f061,end=0x56f1f2,esi=body);color=m.get(STACK+0x1c)
   rows.append(dict(base=base,delta=delta,ticks=ticks,strength=strength,color=color))
   lines.append('    {{{'+','.join(map(str,base))+'},{'+','.join(map(str,delta))+'}},'+str(ticks)+'U,'+repr(strength)+'F,'+str(color)+'U},')
lines+=['};'];text='\n'.join(lines)+'\n';fixture=root/'tests/career_neon_native.inc'
if a.write_fixture:fixture.write_text(text,encoding='ascii')
else:assert fixture.read_text(encoding='ascii')==text,'neon fixture stale'
assert not m.events
(a.out/'neon.json').write_text(json.dumps(dict(cases=rows,hooks=m.events,boundary='Native56F061..56F1F2, actual564BB0/564B10 sine and674898 conversion. Supplied base/delta color fields and ground strength; does not certify the producer, whole-frame blend or console pixels.'),indent=2)+'\n',encoding='ascii')
print('PASS original neon color:',len(rows),'actual-record configurations; no hooks')
