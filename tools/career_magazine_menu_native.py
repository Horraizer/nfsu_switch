"""PC1.4 routing/input/text witness, stopped at viewer/transition requests.
UI shell, photo identifiers, asset lookup and skipped layout callbacks are synthetic.
No live game, rendered original UI, movie or hardware equivalence is claimed.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from audio312_native import Machine,x
from world_body_trace import Image,jdlz
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--write-fixture',action='store_true');args=p.parse_args();root=args.root
raw = (root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im = Image(raw)
lang = (root/'Languages/English.bin').read_bytes()
if lang[:4] == b'JDLZ':
    lang = jdlz(lang)
assert hashlib.sha256(lang).hexdigest() == '6cbab64235523149b95966b1844cdca9fb858744126f8d9ad10e72c921ab8790'
count, rows, text = struct.unpack_from('<III',lang,12)
labels = dict(struct.iter_unpack('<II',lang[rows+8:rows+8+8*count]))
def label(key):
    return lang[text+8+labels[key]:].split(b'\x00',1)[0].decode('cp1252')
assert label(0x731eeb7b) == 'Hot off the press!'
obj, item, second = 0x1100000, 0x1101000, 0x1102000

def routing(authored, pending, seen):
    m = Machine(im, [(0x4ab640,0x4ab663),(0x4ab770,0x4ab783),(0x4ab800,0x4ab82c),(0x51ba80,0x51bab2)])
    m.wi(obj+0x74,int(authored)); m.wi(obj+0x38,1)
    m.uc.mem_write(0x76323d,bytes([pending]));m.uc.mem_write(0x76777c+11,bytes([seen]))
    for addr in (0x4ab781,0x4ab828):
        m.callbacks[addr] = lambda cpu: cpu.finish()
    m.run(0x4ab800, regs={x.UC_X86_REG_EDI:obj})
    return m.cstring(m.uc.reg_read(x.UC_X86_REG_EAX))

routes = [routing(*args) for args in [(True,False,False),(False,True,False),(False,True,True),(False,False,False)]]
assert routes == ['MU_UG_MagazineGiveth.fng']*2+['MU_UG_UndergroundMenu.fng']*2, routes

def button(name):
    result = 0xffffffff
    for b in name.encode('ascii'):
        result = (result*33+b)&0xffffffff
    return result

m = Machine(im,[(0x4c8bcf,0x4c8bd6),(0x4c9030,0x4c90b7),(0x4c9150,0x4c9238),
                (0x4c9240,0x4c9410),(0x4f6910,0x4f6952)])
m.callbacks[0x4c8bd6] = lambda cpu: cpu.finish()
m.run(0x4c8bcf, regs={x.UC_X86_REG_EBP:obj})
flag = lambda: m.uc.mem_read(obj+0x88,1)[0]
assert flag() == 1
m.wi(obj+0x7c,item);m.wi(obj+0x80,item);m.wi(obj+0x84,second)
m.wi(item,second);m.wi(item+4,obj+0x80);m.wi(second,obj+0x80);m.wi(second+4,item)
for node, key, photo in [(item,11,901),(second,14,927)]:
    m.wi(node+0xc,key);m.wi(node+8,photo)
for key in (11,14):
    m.uc.mem_write(0x76777c+key,b'\x00')
m.wi(0x73578c,0)
send = lambda code: m.run(0x4c9240,(code,0,0,0),{x.UC_X86_REG_ECX:obj})
send(button('PAD_RIGHT'));assert m.ri(obj+0x7c)==item
send(button('PAD_BUTTON9'));assert flag()==0 and m.uc.mem_read(0x76777c+11,1)[0]==0
assert 0x4c93b0 not in m.visited
# Skip visual updates after the real list-navigation operation.
m.callbacks[0x4f8910] = lambda cpu: cpu.return_stub()
m.callbacks[0x4c90c0] = lambda cpu: cpu.return_stub()
send(button('PAD_RIGHT'));assert m.ri(obj+0x7c)==second
# Supply a resident UI shell; intercept its asset lookup and final viewer request.
m.wi(0x73578c,0x1104000);m.wi(0x1103000+0x18,0x1105000)
def lookup(cpu):
    cpu.return_stub(0x1103000)
    cpu.uc.reg_write(x.UC_X86_REG_ESP,cpu.uc.reg_read(x.UC_X86_REG_ESP)+4)
m.callbacks[0x4f3f90] = lookup
viewer=[]
def view(cpu):
    viewer.append(cpu.cstring(cpu.uc.reg_read(x.UC_X86_REG_EDI)));cpu.finish()
m.callbacks[0x4f0800] = view
send(button('PAD_BUTTON9'))
assert viewer==['DummyMagazine.fng'] and m.ri(0x735e50)==927
assert list(m.uc.mem_read(0x76777c+11,1))==[0] and list(m.uc.mem_read(0x76777c+14,1))==[1]

for start,code,expected in [(item,'PAD_LEFT',item),(second,'PAD_RIGHT',second),(item,'PAD_RIGHT',second),(second,'PAD_LEFT',item)]:
    m.wi(obj+0x7c,start);send(button(code));assert m.ri(obj+0x7c)==expected
# Real terminal back acknowledges the complete supplied delivery list.
d=Machine(im,[(0x4c9240,0x4c94b0)]);pending=[11,14,0]
d.wi(0x735e58,len(pending));d.uc.mem_write(0x6fae18,bytes(pending)+bytes(16-len(pending)));d.uc.mem_write(0x76777c,bytes(27))
d.callbacks[0x4c96f0]=lambda cpu:cpu.finish();d.run(0x4c9240,(0x911ab364,),{x.UC_X86_REG_ECX:obj})
assert d.ri(0x735e58)==0 and all(d.uc.mem_read(0x76777c+i,1)[0]==1 for i in pending)
rows=[struct.unpack('<I',im.read(0x6fa0b0+4*i,4))+struct.unpack('<I',im.read(0x6fa120+4*i,4)) for i in range(27)]
fixture='// OriginalPC1.4 6FA0B0 description and6FA120 title keys.\nstatic constexpr unsigned kNativeMagazineText[][2]={\n'+''.join('    {0x%08XU,0x%08XU},\n'%row for row in rows)+'};\n'
path=Path(__file__).resolve().parent.parent/'tests/career_magazine_menu_native.inc'
if args.write_fixture:path.write_text(fixture,encoding='ascii')
else:assert path.read_text(encoding='ascii')==fixture,'native magazine labels differ'
assert all(label(a) and label(b) for a,b in rows)
print(json.dumps(dict(routes=routes,title=label(0x731eeb7b),first_confirm='unlock list only',next_confirm=viewer[0],selected_photo=m.ri(0x735e50),navigation='clamped',back='whole supplied batch acknowledged',text_rows=len(rows))))
# Original album4E9730 uses a two-column grid and a scrolling eight-cell
# window. Stop after cursor mutation, before visual refresh; these are real
# navigation instructions with a supplied UI shell, not a rendered game.
def album_input(cell,first,key):
    grid=Machine(im,[(0x4e9730,0x4e9c1b)])
    grid.wi(obj+0x40,cell-first);grid.wi(obj+0x44,first);grid.wi(obj+0x4c,27)
    grid.wi(0x6f1dd8,0)
    grid.callbacks[0x4e8d90]=lambda cpu:cpu.finish()
    grid.callbacks[0x4e9c0e]=lambda cpu:cpu.finish()
    grid.callbacks[0x414a60]=lambda cpu:cpu.return_stub()
    grid.callbacks[0x414a10]=lambda cpu:cpu.return_stub()
    grid.run(0x4e9730,(button(key),0,0,0),{x.UC_X86_REG_ECX:obj})
    raw_cell=grid.ri(obj+0x40)+grid.ri(obj+0x44)
    # The FEng update message performs the real last-cell correction at
    # 4E9A9E. Only its base UI-shell update is stubbed; cursor/window code runs.
    grid.callbacks[0x4f7d30]=lambda cpu:cpu.return_stub()
    grid.run(0x4e9730,(0xc98356ba,0,0,0),{x.UC_X86_REG_ECX:obj})
    after=grid.ri(obj+0x40)+grid.ri(obj+0x44)
    assert 0<=after<27
    if raw_cell>=27:assert (cell,first,key,raw_cell,after)==(25,18,'PAD_DOWN',27,26)
    return after,grid.ri(obj+0x44)
constructor=Machine(im,[(0x4e9662,0x4e967a)])
constructor.wi(obj+0x40,17);constructor.wi(obj+0x44,12)
constructor.callbacks[0x4e9679]=lambda cpu:cpu.finish()
constructor.run(0x4e9662,regs={x.UC_X86_REG_ESI:obj,x.UC_X86_REG_EBX:0})
assert constructor.ri(obj+0x40)==0 and constructor.ri(obj+0x44)==0 and constructor.ri(obj+0x4c)==27
album_moves=[];album_first=[]
for cell in range(27):
    current,first=0,0
    for unused in range(cell//2):current,first=album_input(current,first,'PAD_DOWN')
    if cell%2:current,first=album_input(current,first,'PAD_RIGHT')
    assert current==cell
    results=[];windows=[first]
    for key in ('PAD_LEFT','PAD_RIGHT','PAD_UP','PAD_DOWN'):
        after,window=album_input(cell,first,key)
        results.append(after);windows.append(window)
    album_moves.append(results);album_first.append(windows)
album_fixture='// OriginalPC1.4 4E9730 album destinations: left, right, up, down.\nstatic constexpr unsigned kNativeMagazineAlbumMoves[][4]={\n'+''.join('    {%d,%d,%d,%d},\n'%tuple(row) for row in album_moves)+'};\n'
album_fixture+='static constexpr unsigned kNativeMagazineAlbumFirst[][5]={\n'+''.join('    {%d,%d,%d,%d,%d},\n'%tuple(row) for row in album_first)+'};\n'
album_path=path.with_name('career_magazine_album_native.inc')
if args.write_fixture:album_path.write_text(album_fixture,encoding='ascii')
else:assert album_path.read_text(encoding='ascii')==album_fixture,'native album directions differ'
assert album_moves[2][0]==2 and album_moves[1][1]==1
# Different starting windows have different visible outcomes. A raw scroll
# from cell25/window18 reaches27, then the same native UI update corrects it
# to26. Cell25/window20 rejects Down without changing the visible selection.
assert album_moves[25][3]==26 and album_input(25,20,'PAD_DOWN')==(25,20)
print('PASS original album constructor, all visible grid directions and row-scroll windows')
print('Native visible edge: cell25 first18 Down -> cell26 first20 after UI update; cell25 first20 Down stays25')
print('PASS original magazine routing, notice/list/selection, selected-photo request, back and language tables')
