"""Original PC USA 1.4 wheel paint, turbo gauge and drift award witnesses.

Original pixel arithmetic executes without hooks. The turbo witness intercepts
SAT output. Drift keeps authored zones/ratings and declares the actor, UI and
zone-query boundaries; it does not claim to reproduce a whole driven race.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

sys.dont_write_bytecode = True
from world_body_trace import Image, chunks, jdlz
from physics256_native import Machine, STACK, STOP, bits


def machine(image):
    m = Machine(image)
    m.allowed = [(va, va + size) for va, size, _ in image.sections if 0x400000 <= va < 0x690000]
    m.run(0x5b065b, (0,))
    return m


def swap_rb(pixel):
    return (pixel & 0xff00ff00) | ((pixel & 255) << 16) | ((pixel >> 16) & 255)


def wheel_rows(image):
    m = machine(image)
    src, mask, dest = 0x1100000, 0x1101000, 0x1102000
    rows = []
    for source in (0x00000000, 0xffc0e0f0, 0x9ad1dcee, 0xffffffff, 0x4f0001ff, 0xccffffff):
        for paint in (0xffffffff, 0x80808080, 0x800f0f8d, 0x8045300c, 0x80000000):
            for amount in (0, 1, 2, 63, 127, 128, 253, 254, 255):
                m.put(src, swap_rb(source)); m.put(mask, amount); m.put(dest, 0)
                m.put(STACK + 0x10, mask-src); m.put(STACK + 0x14, dest+4)
                m.run(0x5aa730, end=0x5aa7bf, ebx=src, ebp=dest, edx=mask-src, esi=swap_rb(paint))
                rows.append([source, amount, paint, swap_rb(m.get(dest))])
    assert not m.events
    return rows


def turbo_rows(image):
    rows = []
    for drag in (0, 1):
        for pressure in (-1., -.5, 0., .25, .5, 1., 1.5):
            m = machine(image)
            controller, actor, driver, turbo, dial = (0x1100000, 0x1101000, 0x1102000, 0x1103000, 0x1104000)
            m.put(controller+4, actor); m.put(controller+16, 1)
            m.put(actor+0x28, 1); m.put(actor+0x34, driver); m.put(driver+0x48, turbo)
            m.put(turbo+0xa4, pressure, 'f'); m.put(dial+0x18, 1); m.put(dial+0x10, 1)
            m.put(0x78a344, drag, 'B')
            captured = []
            m.hook(0x4a14d0, 'turbo visibility output', value=0)

            def rotate(cpu):
                captured.append(cpu.get(cpu.reg('esp')+4)); cpu.ret()

            m.hook(0x4f6a70, 'SAT rotation output', callback=rotate)
            m.run(0x4a13a0, eax=controller, ecx=dial)
            assert len(captured) == 1
            rows.append([drag, bits(pressure), captured[0]])
    return rows


def native_draw(m):
    # Keep the genuine x87 return and store, the same boundary as43E2E2.
    at = STOP + 0x400
    code = b'\x68' + struct.pack('<I', bits(1.0)) + b'\xe8' + struct.pack('<i', 0x564a80-(at+10))
    code += b'\x83\xc4\x04\xd9\x1d' + struct.pack('<I', STOP+0x500) + b'\xc3'
    m.uc.mem_write(at, code)
    m.allowed.append((at, at+len(code)))
    m.run(at)
    return m.get(STOP+0x500)


def drift_rows(image, root):
    front = jdlz((root/'FrontEnd/FrontB.lzc').read_bytes())
    glob = (root/'Global/GLOBALB.BUN').read_bytes()
    assert hashlib.sha256(front).hexdigest() == '94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
    assert hashlib.sha256(glob).hexdigest() == '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    a,z = next((a+8,z) for t,a,z in chunks(front) if t==0x34a02)
    events = {struct.unpack_from('<I',front,o)[0]:front[o:o+332] for o in range(a,z,332)}
    a,z = next((a+8,z) for t,a,z in chunks(glob) if t==0x34b00)
    table = glob[a:z]
    world, actor, timing, context, style, zone, entries = (0x1100000+i*0x1000 for i in range(7))
    rows = []
    for event, track in ((3,1301),(13,1302)):
        raw = events[event]
        track_data = jdlz((root/f'Tracks/TRACKB{track}.lzc').read_bytes())
        a,z = next((a+8,z) for t,a,z in chunks(track_data) if t==0x3414a)
        zones = [(i,track_data[o:o+180]) for i,o in enumerate(range(a,z,180)) if struct.unpack_from('<I',track_data,o)[0]==6]
        assert zones
        for difficulty in range(3):
            for grade in range(4):
                m = machine(image)
                m.put(0x7361f8,world);m.put(context+8,actor)
                m.uc.mem_write(0x779c78,table)
                m.uc.mem_write(0x78a380,raw[0x13c+4*difficulty:0x140+4*difficulty])
                # Test the normal saturated scale and a low-performance case.
                scale = 1. if grade!=3 else .6499999761581421
                m.put(0x700a18,scale,'f')
                m.put(0x6f227c,0x60000000)
                m.put(style+0x28,123.5,'f')
                m.hook(0x4081e0,'actor count or timing accessor',callback=lambda c:c.ret(value=1 if c.reg('eax')==world else timing))
                m.hook(0x401d80,'physical human roster',value=actor)
                m.hook(0x424d60,'human actor role',value=1)
                m.hook(0x424db0,'style owner accessor',value=style)
                m.hook(0x432d40,'style item index owner',value=style)
                m.hook(0x575950,'drift style item0',value=style)
                m.hook(0x41f5c0,'human id',value=0)
                m.hook(0x43ea50,'find standings row',callback=lambda c:c.ret(value=entries+0x40*(c.get(c.reg('esp')+4)-99) if c.get(c.reg('esp')+4)>=100 else entries))
                m.hook(0x43a2e0,'single player competition',value=0)
                m.hook(0x43f0e0,'first containing authored zone',value=zone,pop=4)
                m.hook(0x401bd0,'skip external standings UI sort',value=1)
                m.hook(0x432c50,'skip external lap-result output',value=0)
                # Keep the initialization draw separate from all award ticks.
                native_draw(m)
                for i in range(3):
                    p = entries+(i+1)*0x40
                    m.put(p+0xc,grade)
                    m.put(p+0x20,struct.unpack_from('<h',raw,0xe4+28*i+2*difficulty)[0])
                    m.put(p+0x24,native_draw(m))
                # Cover first/repeated/backward visits in both events, plus
                # the complete four-lap award budget on the second track.
                visits = [(lap, *zones[0]) for lap in (0,0,1,0,1,2,3)]
                if event == 13 and grade == 3:
                    visits = [(lap, zi, raw_zone) for lap in range(4) for zi, raw_zone in zones]
                counts = {}
                for lap, zi, raw_zone in visits:
                    m.uc.mem_write(zone,raw_zone);m.put(zone+0x1a,counts.get(zi,0),'h')
                    m.put(timing+9,lap,'B')
                    m.run(0x43eb00,ecx=context)
                    counts[zi] = m.get(zone+0x1a,'h')
                    assert m.get(entries+0x10)==bits(123.5)
                    rows.append([event,difficulty,grade,zi,lap,*[m.get(entries+i*0x40+0x10) for i in range(1,4)]])
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('retail_root',type=Path);p.add_argument('output',type=Path);p.add_argument('--write-fixture',action='store_true')
    a=p.parse_args();output=a.output.resolve();root=Path.cwd()
    relative=output.relative_to(root)
    assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
    assert a.retail_root.resolve() not in output.parents and not output.exists()
    raw=(a.retail_root/'speed.exe').read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image=Image(raw)
    report={'WheelPaint':wheel_rows(image),'TurboAngles':turbo_rows(image),'DriftScores':drift_rows(image,a.retail_root)}
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
    text='// Original PC USA 1.4; regenerate with original_feedback_native.py.\n'
    for name,rows in report.items():
        text+=f'static constexpr unsigned kNative{name}[][{len(rows[0])}]={{\n'
        text+=''.join(' {'+','.join(f'0x{x:08x}U' for x in row)+'},\n' for row in rows)+'};\n'
    fixture=root/'tests/original_feedback_native.inc'
    if a.write_fixture:fixture.write_text(text,encoding='ascii')
    else:assert fixture.read_text(encoding='ascii')==text
    print('PASS',', '.join(f'{k}={len(v)}' for k,v in report.items()))


if __name__=='__main__':main()

