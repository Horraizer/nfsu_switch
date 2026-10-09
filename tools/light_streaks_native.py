"""Capture original Light Streaks pass scheduling; GPU calls are boundaries."""
import argparse,hashlib,sys,json
from pathlib import Path
sys.dont_write_bytecode=True
from event0_trace import Image
from hud254_native import Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
raw=(parser.parse_args().retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);im.sections=[(a,s,r) for a,s,r,n in im.sections]
m=Machine(im);m.write(0x73645c,1)
m.write(0x73636c,0x1000100);m.write(0x1000100,0x1001000)
for offset in [0x94,0xe4]:m.write(0x1001000+offset,0x1002000+offset)
m.write(0x71abb0,0x1008000);m.write(0x71aba4,0x1008100)
m.write(0x1005000,0x1005100);m.write(0x1005118,0x1007000)
calls=[]
boundaries={0x5461c0:'white_texture',0x40d6e0:'fullscreen',0x40e6d0:'effect',
0x544f30:'world_lights',0x4ae7d0:'auxiliary_lights',0x570d00:'vehicle_list',
0x570c70:'vehicle_other',0x40abd0:'restore_target',0x1002094:'target',0x10020e4:'render_state'}
def capture(uc,at,size,data):
    if at not in boundaries:return
    esp=uc.reg_read(UC_X86_REG_ESP);name=boundaries[at]
    args=[m.integer(esp+4+4*i) for i in range(6)]
    record={'call':name}
    if name=='fullscreen':
        record.update(count=args[0],texture=m.integer(args[1]),color=m.integer(args[2]),
                      blend_enabled=args[3],src=args[4],dst=args[5])
    elif name=='render_state':record.update(state=args[1],value=args[2])
    elif name=='target':record.update(surface=args[2])
    calls.append(record)
    cleanup=12 if name in ('target','render_state') else 8 if name=='auxiliary_lights' else 0
    if name=='white_texture':uc.reg_write(UC_X86_REG_EAX,0x1005000)
    uc.reg_write(UC_X86_REG_EIP,m.integer(esp));uc.reg_write(UC_X86_REG_ESP,esp+4+cleanup)
m.uc.hook_add(UC_HOOK_CODE,capture)
try:m.run(0x409b70)
except Exception:
    print(json.dumps(calls,indent=2));print("EIP",hex(m.uc.reg_read(UC_X86_REG_EIP)),"ESP",hex(m.uc.reg_read(UC_X86_REG_ESP)));raise
full=[x for x in calls if x['call']=='fullscreen']
assert len(full)==2 and (full[0]['src'],full[0]['dst'])==(1,14) and (full[1]['src'],full[1]['dst'])==(2,2)
assert full[0]['color']==full[1]['color']==0x1fffffff
assert any(x.get('state')==193 and x.get('value')==0x008296b4 for x in calls)
assert [x['call'] for x in calls if 'lights' in x['call'] or 'vehicle' in x['call']]==['world_lights','auxiliary_lights','vehicle_list','vehicle_other']
print(json.dumps(calls,indent=2))
calls.clear();m.write(0x73645c,0);m.run(0x409b70);assert not calls
print('PASS: original persistent target, RGB blend factor, light producers, additive composite and non-main gate')
print('BOUNDARY: D3D/effect/light draw calls captured, not executed GPU pixels; main-view type supplied')
