"""Bounded original drag heat and shift-quality consumers, PC 1.4 USA."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from hud254_trace import PE
from hud254_native import Machine
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_FPCW, UC_X86_REG_ESI, UC_X86_REG_EBX


def bits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'output', 'evidence'):
        ap.add_argument('--' + name, type=Path, required=True)
    args = ap.parse_args()
    data = (args.root / 'speed.exe').read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    image = PE(data)
    engine, setup, box, race = 0x1001000, 0x1002000, 0x1003000, 0x1004000
    heat, quality, boost = [], [], []
    for sequence in range(4):
        m = Machine(image)
        m.uc.reg_write(UC_X86_REG_FPCW, 0x007f)
        m.write(engine + 0x18, setup)
        m.write(engine + 0x20, box)
        m.write(setup, bits(1000))
        m.write(setup + 4, bits(7000))
        m.write(setup + 8, bits(8000))
        m.write(0x73619c, race)
        m.write(0x78a344, 1)
        m.write(0x7361a8, 0)
        frames = []
        for step in range(240):
            clock = 40000 + step * 67
            held = step < 10
            gear = 2 if sequence == 2 else 3
            q = .85 if sequence == 2 and step < 90 else 0.
            rpm = 7500. if sequence != 1 or step < 140 else 6500.
            if sequence == 3:
                rpm = 7500. if step % 30 < 10 else 6500.
            angular = bits(rpm * struct.unpack('<f', image.read(0x6ccab4, 4))[0])
            m.write(engine + 0x14, angular)
            m.write(box + 0x3c, gear)
            m.write(box + 0x68, bits(q))
            m.write(0x73ad34, clock)
            m.write(race, 3 if held else 4)
            m.run(0x46ca90, registers={UC_X86_REG_ECX: engine})
            frames.append([clock, held, gear, bits(q), angular,
                           *[m.integer(engine + x) for x in (0x8c, 0x90, 0x98, 0x94)]])
        heat.append(frames)
    for category in range(4):
        for gear in (1, 2, 4, 6):
            for scale in (1., 1.5, 3.):
                for delta in (-2000., -400., -150., 0., 1., 10., 40., 100.):
                    m = Machine(image)
                    m.uc.reg_write(UC_X86_REG_FPCW, 0x007f)
                    m.write(0x78a344, 1)
                    m.write(box + 0x3c, gear)
                    m.write(box + 0x38, gear + 1)
                    m.write(box + 0xe0, category)
                    m.write(box + 0xd8, bits(scale))
                    m.write(box + 0x80 + gear * 4, bits(7000.))
                    m.run(0x46e940, (bits(7000. + delta),), {UC_X86_REG_ECX: box})
                    quality.append([bits(7000. + delta), bits(7000.), category, gear,
                                    bits(scale), m.integer(box + 0x68)])
    for mode in (0, 2, 3):
        for q in (-1., 0., .5, .85, 1.):
            for ticks in (40000, 40067, 41000, 41999, 42000, 42001):
                m = Machine(image)
                m.uc.reg_write(UC_X86_REG_FPCW, 0x007f)
                m.write(box + 0x20, engine)
                m.write(box + 0x44, mode)
                m.write(box + 0x64, bits(10.))
                m.write(box + 0x68, bits(q))
                m.write(engine + 0x80, bits(1.))
                m.write(0x73ad34, ticks)
                # Supplied common-tail state, original46DEC3..46DF86. Its
                # epilogue pops five saved words; no called function is hooked.
                m.run(0x46dec3, (0, 0, 0, 0, m.stop), {UC_X86_REG_ESI: box, UC_X86_REG_EBX: 0})
                boost.append([mode, bits(q), ticks, m.integer(engine + 0x80),
                              m.integer(box + 0x64), m.integer(box + 0x68)])
    def literal(v):
        if isinstance(v, bool):
            return 'true' if v else 'false'
        if isinstance(v, list):
            return '{' + ','.join(map(literal, v)) + '}'
        return str(v) + 'u'
    args.output.write_text('// Original46CA90/46E940 with x87 control007F, no hooks.\n' +
                          'const DragHeatCase drag_heat_cases[4][240]=' + literal(heat) + ';\n' +
                          'const DragQualityCase drag_quality_cases[]=' + literal(quality) + ';\n' +
                          'const DragBoostCase drag_boost_cases[]=' + literal(boost) + ';\n', encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha, hooks=[], heat=heat, quality=quality, boost=boost)) + '\n', encoding='ascii')
    print('Native drag heat:', sum(map(len, heat)), 'updates; shift quality:', len(quality), 'boost:', len(boost), '; hooks: 0')


if __name__ == '__main__':
    main()
