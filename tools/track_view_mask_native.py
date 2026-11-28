"""Execute retail PC 53E290 mask construction on explicitly supplied contexts."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from world_body_trace import Image
from physics256_native import Machine, CONFIG, ACTOR


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--write-fixture', action='store_true')
    args = parser.parse_args()
    work = Path.cwd().resolve()
    out = args.out.resolve()
    relative = out.relative_to(work)
    assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
    assert args.root.resolve() not in (out, *out.parents)
    out.mkdir(parents=True, exist_ok=False)
    exe = (args.root / 'speed.exe').read_bytes()
    digest = hashlib.sha256(exe).hexdigest()
    assert digest == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image = Image(exe)
    rows = []
    for view in (0, 1, 2, 4, 9, 10, 15, 16):
        for race_type in range(6):
            for direction in (0, 1, 2):
                for supplied in (0, 0x800, 0x1000, 0x1800, 0x2000):
                    m = Machine(image)
                    m.allowed = [(0x53e290, 0x53e323)]
                    # The renderer's context initializer is outside the mask
                    # calculation. It receives the destination pointer; its
                    # matrix/queue fields are irrelevant to the mask at +84.
                    m.hook(0x408e30, 'external-context-initializer', callback=lambda n: n.ret())
                    m.put(ACTOR + 4, view)
                    m.put(0x7364a0, race_type)
                    m.put(0x78a2fc, direction)
                    m.run(0x53e290, (CONFIG,), ecx=ACTOR, eax=supplied)
                    assert len(m.events) == 1
                    rows.append([view, race_type, direction, supplied, m.get(CONFIG + 0x84)])
    gates=[]
    for mask in sorted({r[4]&255 for r in rows}):
        for flags in (0,2,4,8,0x10,0x20,0x40,0x50,0x60,0x64,0x68,0x70,0x460,0xFFFF):
            m=Machine(image)
            m.allowed=[(0x445eda,0x445ef1)]
            def rejected(n):
                n.finished=True
                n.uc.emu_stop()
            # Stop before the omitted draw/epilogue path, after the real JNE.
            m.hook(0x446133,'rejected-before-emission',callback=rejected)
            m.put(ACTOR+14,flags,'H')
            m.put(CONFIG+0x84,mask)
            m.run(0x445eda,end=0x445ef1,edi=CONFIG,ebx=ACTOR)
            gates.append([mask,flags,int(0x445ef1 in m.visited)])
    text = '// Retail PC 53E290; only external408E30 context initialization is stubbed.\n'
    text += 'struct NativeTrackViewMask {unsigned view,type,direction,supplied,result;};\n'
    text += 'static constexpr NativeTrackViewMask kNativeTrackViewMasks[]={\n'
    text += ''.join('    {%s},\n' % ','.join('%dU' % v for v in row) for row in rows)
    text += '};\n'
    text += '// Real445EDA..445EEB, stopped at either branch before emission.\n'
    text += 'struct NativeTrackInstanceGate {unsigned mask,flags,accepted;};\n'
    text += 'static constexpr NativeTrackInstanceGate kNativeTrackInstanceGates[]={\n'
    text += ''.join('    {%s},\n' % ','.join('%dU' % v for v in row) for row in gates)
    text += '};\n'
    fixture = work / 'tests/track_view_mask_native.inc'
    if args.write_fixture:
        fixture.write_bytes(text.encode('ascii'))
    else:
        assert fixture.read_bytes() == text.encode('ascii'), 'native fixture is stale'
    report = dict(source='retail PC 1.4 USA speed.exe', sha256=digest,
                  executed_range=['0x53E290','0x53E323'],
                  range_sha256=hashlib.sha256(exe[0x13e290:0x13e323]).hexdigest(),
                  external_call='0x408E30 context initializer stub; mask arithmetic unhooked',
                  boundary='synthetic contexts, not original runtime residency or pixels',
                  fields=['view','type','direction','supplied','result'], rows=rows,
                  gate_range=['0x445EDA','0x445EF1'],gate_fields=['mask','flags','accepted'],gates=gates)
    (out / 'oracle.json').write_text(json.dumps(report, indent=2)+'\n',encoding='ascii')
    print('PASS original53E290: %d contexts; original445EE9: %d gates' % (len(rows),len(gates)))


if __name__ == '__main__':
    main()
