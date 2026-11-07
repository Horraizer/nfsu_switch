"""Original4DC280 input navigation through4DC230 and4DBAD0.
Synthetic populated/missing palette cells; stop before4DBF90's FEng rendering.
No call hooks, no retail writes. This does not execute a complete front end.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
sys.dont_write_bytecode=True
root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from world_body_trace import Image
from physics256_native import Machine
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--write-fixture',action='store_true');a=ap.parse_args()
relative=a.out.resolve().relative_to(root);assert relative.as_posix().isascii() and relative.parts[0].startswith('build');a.out.mkdir(parents=True,exist_ok=False)
raw=(a.root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
m=Machine(Image(raw));m.allowed=[(0x4dc230,0x4dc390),(0x4dbad0,0x4dbb3f)]
controller=0x1100000;rows=[];bases=[0x5c,0x214,0x3cc,0x584,0x73c,0x8f4]
directions=[0x9120409e,0xb5971bf1,0x72619778,0x911c0a4b]
for family,first in enumerate([0,10,30]):
 cells=list(range(first,60)) if family!=1 else list(range(10,20))+list(range(30,50))
 # Metallic's10 level2 and20 level3 records use the first cells of each tier.
 for start in cells:
  for direction,event in enumerate(directions):
   m.uc.mem_write(controller,bytes(0xc00));m.put(controller+0xb04,6);m.put(controller+0xb08,10)
   for cell in cells:m.put(controller+bases[cell//10]+(cell%10)*0x2c+0x10,0x1110000)
   m.put(controller+0xb0c,start//10);m.put(controller+0xb10,start%10)
   m.run(0x4dc280,end=0x4dc37e,esi=controller,edi=event)
   result=m.get(controller+0xb0c)*10+m.get(controller+0xb10);assert result in cells
   rows.append([family,start,direction,result])
assert len(rows)==480 and not m.events
# Original4DAFA0's channel transitions save the active cell before restoring
# the destination. Run only the memory-transform slice before FEng calls.
cursor=Machine(Image(raw));cursor.allowed=[(0x4db1d7,0x4db248),(0x4db4eb,0x4db573)]
cursor_rows=[]
frame=0x1110000
for channels in range(2,5):
 for channel in range(channels):
  for target in (channel-1,channel+1):
   if not 0<=target<channels:continue
   for start in (0,9,17,35,59):
    cursor.uc.mem_write(controller,bytes(0xc00))
    cursor.put(controller+0xb20,channels);cursor.put(controller+0xb24,channel)
    cursor.put(controller+0xb28,0)
    cursor.put(controller+0xb0c,start//10);cursor.put(controller+0xb10,start%10)
    destination=(start+23)%60
    cursor.put(controller+0xb78+target*4,destination//10)
    cursor.put(controller+0xb38+target*4,destination%10)
    cursor.put(frame-4,controller)
    if target>channel:cursor.run(0x4db1d7,end=0x4db248,ebp=frame,edi=target)
    else:cursor.run(0x4db4eb,end=0x4db573,esi=controller)
    saved=cursor.get(controller+0xb78+channel*4)*10+cursor.get(controller+0xb38+channel*4)
    restored=cursor.get(controller+0xb0c)*10+cursor.get(controller+0xb10)
    assert saved==start and restored==destination
    cursor_rows.append([channels,channel,target,start,restored])
assert not cursor.events
finishes=Machine(Image(raw));finishes.allowed=[(0x4db7df,0x4db851)]
finish_rows=[]
for family in range(3):
 for target in ((family-1)%3,(family+1)%3):
  for start in (0,9,17,35,59):
   finishes.uc.mem_write(controller,bytes(0xc00))
   finishes.put(controller+0xb20,1);finishes.put(controller+0xb28,target)
   finishes.put(controller+0xb0c,start//10);finishes.put(controller+0xb10,start%10)
   destination=(start+11)%60
   finishes.put(controller+0xb78+target*4,destination//10)
   finishes.put(controller+0xb38+target*4,destination%10)
   finishes.run(0x4db7df,end=0x4db851,esi=controller,ecx=1,ebx=family)
   saved=finishes.get(controller+0xb78+family*4)*10+finishes.get(controller+0xb38+family*4)
   restored=finishes.get(controller+0xb0c)*10+finishes.get(controller+0xb10)
   assert saved==start and restored==destination
   finish_rows.append([family,target,start,restored])
assert not finishes.events
fixture='// Original4DC280/4DC230/4DBAD0; stop before rendering, no hooks.\nstatic constexpr unsigned kNativePaletteNavigation[][4]={\n'+'\n'.join('    {'+','.join(str(v)+'U' for v in row)+'},' for row in rows)+'\n};\n'
icons=Machine(Image(raw));icons.allowed=[(0x68d1d0,0x68d4d0),(0x567c70,0x567ca0)]
icons.run(0x68d1d0,end=0x68d4ce)
values=[icons.get(0x746918+4*i) for i in range(19)];assert all(values) and not icons.events
fixture+='// Original68D1D0 initializer for4E24A0; no call hooks.\nstatic constexpr unsigned kNativeVinylIcons[]={'+','.join(str(v)+'U' for v in values)+'};\n'
p=root/'tests/career_palette_native.inc' 
if a.write_fixture:p.write_text(fixture,encoding='ascii')
else:assert p.read_text(encoding='ascii')==fixture
(a.out/'evidence.json').write_text(json.dumps(dict(scope=__doc__,cases=rows,hooks=m.events,vinyl_icons=values,cursor_revisits=cursor_rows,cursor_hooks=cursor.events,finish_revisits=finish_rows,finish_hooks=finishes.events,retail_sha256=hashlib.sha256(raw).hexdigest()),indent=2)+'\n',encoding='ascii')
print('PASS original palette navigation480 cases; full/sparse tier grids; no call hooks')
print('PASS original channel cursor save/restore:',len(cursor_rows),'cases; no call hooks')
print('PASS original finish cursor save/restore:',len(finish_rows),'cases; no call hooks')
