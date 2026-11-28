"""Read-only original PC/GC NIS wheel arithmetic witnesses; no running game."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from car_shadow_gc_native import load_disc
from world_body_trace import Image
from physics256_native import Machine, STACK
from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
import unicorn.ppc_const as ppc

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--disc', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--write-fixture', action='store_true')
a = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
relative = a.out.resolve().relative_to(repo)
assert relative.parts and relative.parts[0].startswith('build') and relative.as_posix().isascii()
assert a.root.resolve() not in a.out.resolve().parents
a.out.mkdir(parents=True, exist_ok=False)
raw = (a.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im = Image(raw)
loaded, matched = load_disc(a.disc)
gc = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
gc.mem_map(0x80000000, 0x02000000)
for at, data in loaded:
    gc.mem_write(at, data)
gc.reg_write(ppc.UC_PPC_REG_MSR, gc.reg_read(ppc.UC_PPC_REG_MSR) | 0x2000)
allowed = [(0x80087a28, 0x80087a68)]
stops = set()
def guard(uc, at, size, unused):
    if at in stops:
        uc.emu_stop()
        return
    assert any(lo <= at < hi for lo,hi in allowed), hex(at)
gc.hook_add(UC_HOOK_CODE, guard)
def f32(x):
    return struct.unpack('>f', struct.pack('>f', x))[0]
def put(at, x):
    gc.mem_write(at, struct.pack('>f', x))
def fpr(reg, x):
    gc.reg_write(reg, struct.unpack('>Q', struct.pack('>d', x))[0])
def gc_phase(previous, distance, radius):
    for reg, value in {1:0x81700000,14:0x81710000,15:0x81711000,16:0x81712000,19:0x81713000,31:0}.items():
        gc.reg_write(getattr(ppc,'UC_PPC_REG_'+str(reg)),value)
    gc.mem_write(0x817000cc, struct.pack('>I',0x81714000))
    put(0x81710020, radius)
    put(0x81713000, previous)
    fpr(ppc.UC_PPC_REG_FPR29, distance)
    fpr(ppc.UC_PPC_REG_FPR30, f32(3.141590118408203))
    fpr(ppc.UC_PPC_REG_FPR31, 0.0)
    gc.emu_start(0x80087a28,0x80087a68,count=100)
    assert gc.reg_read(ppc.UC_PPC_REG_PC) == 0x80087a68
    return struct.unpack('>f',gc.mem_read(0x81714000,4))[0]
phases = []
for radius in [.21,.28,.31595,.34,.37,.43]:
    for distance in [0,.0002,.03,.5,5,20]:
        for previous in [0,.25,-.5,9.0]:
            row = [f32(x) for x in [previous,distance,radius]]
            row.append(gc_phase(*row))
            phases.append(row)
# Complete original controller guard and half-speed advance, including the
# key-count endpoint and the non-rolling finishing placement. Calls to pose
# evaluation are boundaries; root/terrain data are covered by the retail test.
clocks=[]
for count in [10,31,123]:
    for previous in [0,count-3,count-1,count-.25,count,count+1]:
        for dt in [0,1/144,1/60,1/30,1/15]:
            previous,dt=f32(previous),f32(dt)
            controller,other,base=0x81720000,0x81721000,0x81730000
            for addr in [controller,other]:
                put(addr+0x60,count);put(addr+0x64,previous);put(addr+0x5c,.5)
                gc.mem_write(addr+0x34,struct.pack('>I',0x20))
            put(base-0x764c,30)
            for reg,value in {1:0x81700000,13:base,30:controller,31:other}.items():
                gc.reg_write(getattr(ppc,'UC_PPC_REG_'+str(reg)),value)
            fpr(ppc.UC_PPC_REG_FPR28,dt)
            allowed=[(0x800875a8,0x800875fc)]
            stops={0x800875fc,0x8008760c}
            gc.emu_start(0x800875a8,0,count=100)
            where=gc.reg_read(ppc.UC_PPC_REG_PC)
            assert where in stops
            finished=where==0x800875fc
            if not finished:
                allowed=[(0x800856ac,0x800857fc)]
                stops={0x817f0000}
                gc.reg_write(ppc.UC_PPC_REG_3,controller)
                gc.reg_write(ppc.UC_PPC_REG_LR,0x817f0000)
                fpr(ppc.UC_PPC_REG_FPR1,dt)
                gc.emu_start(0x800856ac,0,count=100)
                assert gc.reg_read(ppc.UC_PPC_REG_PC)==0x817f0000
            frame=struct.unpack('>f',gc.mem_read(controller+0x64,4))[0]
            clocks.append([previous,dt,float(count),frame,float(finished)])
# Complete original PC distance/guard/4-wheel accumulation slice. The pose
# reset and animation sampling are explicit inputs, not emulated game IO.
pc_guards = []
for prior,done,dt,x,y in [(0,0,.016,3,4),(1,0,.016,3,4),(1,0,.016,0,0),(1,1,.016,3,4),(1,0,0,3,4),(1,0,.016,-3,-4)]:
    m = Machine(im)
    m.allowed=[(0x47c82e,0x47c955)]
    car,record,spec=0x1100000,0x1110000,0x1120000
    m.put(STACK+0x1c,record);m.put(STACK+0x10,done)
    m.put(STACK+0x20,x,'f');m.put(STACK+0x24,y,'f');m.put(STACK+0xc,spec)
    for i,p in enumerate([0,.25,-.5,1]):
        m.put(STACK+0x90+4*i,p,'f')
        m.put(spec+0x20+0x30*i,[.28,.31,.34,.37][i],'f')
    m.put(record+0x10,prior);m.put(STACK+0x10c,dt,'f')
    m.run(0x47c82e,end=0x47c955,eax=0,ebx=car,ebp=STACK+0x100)
    values=[m.get(car+0x2c0+4*i,'f') for i in range(4)]
    if not prior or done or not dt or not(x or y):
        assert values == [0,.25,-.5,1]
    else:
        assert all(values[i]>[0,.25,-.5,1][i] for i in range(4))
    pc_guards.append([prior,done,dt,x,y,values])
steering=[]
for now,later in [(0,1),(1000,2000),(2000,1000),(65535,0),(0,65535),(50000,20),(0,16384),(60000,5000),(20,50000)]:
    m=Machine(im);m.allowed=[(0x47c7b3,0x47c854),(0x674898,0x674940)]
    m.put(STACK+0x1c,now);m.run(0x47c7b3,end=0x47c854,eax=later,ebx=0x1100000)
    result=m.get(0x1100000+0x376,'H')
    assert result == (later-now)&65535
    steering.append([now,later,result])
def literal(x):
    s=format(x,'.9g')
    return s+('.0' if '.' not in s and 'e' not in s else '')+'F'
text='// Original GNDE69 UpdateAnimatedCars 80087A28..80087A68 arithmetic.\n'
text+='// Generated by tools/start_car_wheels_native.py; no retail bytes.\n'
text+='static constexpr float kStartWheelNative[][4]={\n'
text+=''.join('    {'+','.join(literal(x) for x in row)+'},\n' for row in phases)
text+='};\n'
text+='// Original GNDE69 controller guard800875A8 and AdvanceAnimTime800856AC.\n'
text+='static constexpr float kStartClockNative[][5]={\n'
text+=''.join('    {'+','.join(literal(x) for x in row)+'},\n' for row in clocks)
text+='};\n'
fixture=repo/'tests/start_car_wheels_native.inc'
if a.write_fixture:
    fixture.write_bytes(text.encode('ascii'))
else:
    assert fixture.read_bytes() == text.encode('ascii'), 'native wheel fixture differs'
report=dict(gc_sections_matched=matched,gc_phase_rows=phases,gc_clock_rows=clocks,pc_guards=pc_guards,pc_steering=steering,
    boundary='Original arithmetic over supplied f32 phases/radii and explicit first/end/time state; no game, emulator process, render device or retail writes')
(a.out/'start-wheel-native.json').write_bytes((json.dumps(report,indent=2)+'\n').encode('ascii'))
print('PASS original GC phase cases='+str(len(phases))+'; GC controllers='+str(len(clocks))+'; PC planar-distance guards='+str(len(pc_guards))+'; PC steering='+str(len(steering)))
