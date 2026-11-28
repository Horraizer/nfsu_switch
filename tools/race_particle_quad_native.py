"""Original404110 draw path: stock births, main view, declared D3D call hooks.

Controls stop after the first real quad stores. This is vertex dispatch and
color evidence, not execution of the original GPU or a complete game frame.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX

import argparse
from hud254_trace import PE

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
raw=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=PE(raw)
bank=(args.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bank).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'

from race_particle_native_world import World

definitions = [bank[0x2034a0+i*544:0x2034a0+(i+1)*544] for i in range(90)]
names = ('XETT_RoadSDr_SprayA', 'XETT_RoadSk_SmokeA', 'XETT_Deepwater_DrMistA')
# Select names from the real group to avoid guessing the authored spelling.
selected = [next(r for r in definitions if struct.unpack_from('<I',r,8)[0] == 0xccf7bd61)]
for group in ('XETT_ROAD_SK', 'XETT_DEEPWATER_DR'):
    selected.append(next(r for r in definitions if r[0x1f0:].split(b'\0',1)[0].decode('ascii') == group))
rows = []
for record in selected:
    w = World(image, False)
    w.put(w.stack, w.done)
    w.reset(0x40b5e0, w.done, ESP=w.stack)
    source = w.source(0, 0, seed=121)
    w.uc.mem_write(w.get(source+0x1c), record)
    owner = w.base+0xb800
    w.put(owner+0x534, 0xffffffff)
    w.provide_particles(984)
    for offset, value in ((0,w.done),(4,w.system),(8,source),(16,owner)):
        w.put(w.stack+offset,value)
    w.put(w.stack+12,-.001,'f')
    w.uc.reg_write(UC_X86_REG_ESP,w.stack)
    w.uc.emu_start(0x402d30,w.done,count=300000)
    assert w.uc.reg_read(UC_X86_REG_EIP)==w.done
    emitter = w.get(source+0x20)
    particle = w.get(emitter+0x14)
    assert particle != emitter+0x14
    original_life = struct.unpack('<h', w.uc.mem_read(particle+8,2))[0]
    w.put(particle+8,max(1,original_life//2),'h')
    device, device_vtable, vb, vb_vtable, camera, view, matrix, texture = (
        w.base+x for x in (0x8000,0x8800,0x9000,0x9800,0xa000,0xb000,0xc000,0xc800))
    states, events = [], []
    stub_state, stub_lock = w.base+0xd000,w.base+0xd020
    vertices = w.base+0x20000
    w.uc.mem_map(vertices,0x10000)
    w.put(0x73636c,device);w.put(device,device_vtable);w.put(device_vtable+0xe4,stub_state)
    w.put(0x7187b4,0);w.put(0x718738,vb);w.put(vb,vb_vtable);w.put(vb_vtable+0x2c,stub_lock)
    w.put(view,matrix);w.put(view+4,1);w.put(view+0x40,camera)
    w.put(camera+0x280,owner);w.put(camera+0x284,0)
    w.uc.mem_write(matrix,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
    w.put(w.get(source+0x1c)+0x14,texture)
    complete = []
    def ret(pop):
        sp=w.uc.reg_read(UC_X86_REG_ESP);target=w.get(sp)
        w.uc.reg_write(UC_X86_REG_ESP,sp+4+pop);w.uc.reg_write(UC_X86_REG_EAX,0)
        w.uc.reg_write(UC_X86_REG_EIP,target)
    def observe(uc,address,size,data):
        if address in (0x40e6d0,0x410400,0x40f3d0):
            events.append(hex(address));ret(0)
        elif address==stub_state:
            sp=uc.reg_read(UC_X86_REG_ESP);states.append([w.get(sp+8),w.get(sp+12)]);ret(12)
        elif address==stub_lock:
            sp=uc.reg_read(UC_X86_REG_ESP);w.put(w.get(sp+16),vertices);events.append('lock');ret(20)
        elif address==0x4048db:
            complete.append('quad');uc.emu_stop()
    hook=w.uc.hook_add(UC_HOOK_CODE,observe)
    for offset,value in ((0,w.done),(4,w.system),(8,view)):w.put(w.stack+offset,value)
    w.uc.reg_write(UC_X86_REG_ESP,w.stack)
    w.uc.emu_start(0x404110,w.done,count=300000)
    ip=w.uc.reg_read(UC_X86_REG_EIP)
    assert ip in (w.done,0x4048db),hex(ip)
    name=record[0x180:].split(b'\0',1)[0].decode('ascii')
    rows.append(dict(name=name,view=1,owner_visible=True,chase=True,
        supplied_remaining_ticks=max(1,original_life//2),birth_life_ticks=original_life,
        passed_quad_stores=bool(complete),ip=hex(ip),states=states,hooks=events,
        vertex_words=list(struct.unpack('<24I',w.uc.mem_read(vertices,96))) if complete else []))
    if complete:
        assert all(rows[-1]['vertex_words'][i] >> 24 for i in (3,9,15,21)), 'quad boundary alone is not visible output'
        assert len({tuple(rows[-1]['vertex_words'][i:i+3]) for i in (0,6,12,18)}) == 4, 'missing real quad geometry'
    print(name,'quad',bool(complete),'states',states,'hooks',events)
out=args.out
(out/'draw-controls-native.json').write_bytes((json.dumps(rows,indent=2)+'\n').encode('ascii'))
assert not rows[0]['passed_quad_stores'] and all(r['passed_quad_stores'] for r in rows[1:])
print('PASS road suppressed; stock skid and deep-water controls reach quad stores in main view')

assert (args.retail_root/'speed.exe').read_bytes()==raw
