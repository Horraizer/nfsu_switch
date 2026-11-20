"""Original44F8A0 retained-target admission, before reaction and RNG gate.

The published Driver path is empty, so the real457590 collision query misses.
All positions and neighborhood fields are declared synthetic inputs. The only
hooks observe admission/rejection; no function result is substituted.
"""
import argparse
import hashlib
import json
from pathlib import Path
from physics256_native import Machine, UC_HOOK_CODE
from world_body_trace import Image
from ai_path_vectors import literal


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'output', 'evidence'):
        ap.add_argument('--' + name, type=Path, required=True)
    args = ap.parse_args()
    data = (args.root / 'speed.exe').read_bytes()
    identity = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest() != identity:
        raise ValueError('unsupported executable')
    image = Image(data)
    ctx, actor, driver, node, ref, state, out = [0x1100000 + j * 4096 for j in range(7)]
    cases = [((8, 0, 0), 0, 8), ((6, 8, 0), 0, 8), ((6, 0, 8), 0, 8),
             ((0, 0, 9), 0, 8), ((0, 0, 10), 0, 8), ((0, 0, 11), 0, 8),
             ((8, 0, 0), -.01, 8), ((8, 0, 0), .02, .02),
             ((8, 0, 0), -.02, .02), ((8, 0, 0), 1, 3.6999998092651367),
             ((8, 0, 0), 1, 3.700000047683716), ((8, 0, 0), 1, 3.700000286102295)]
    rows = []
    lines = ['// Original44F8A0 admission before45D5E0; no semantic hooks.',
             '// speed.exe SHA256 ' + identity]
    for xyz, relative, gap in cases:
        m = Machine(image)
        m.allowed = [(0x44f8a0, 0x44faa0), (0x44fadd, 0x44faea),
                     (0x458f10, 0x4590b0), (0x457590, 0x4576a7),
                     (0x456780, 0x456823), (0x647b70, 0x647b81),
                     (0x44d5d0, 0x44d5f0), (0x567280, 0x5672a9)]
        for address, value in {ctx: actor, ctx + 4: 2, ctx + 0x398: 2,
                ctx + 0x39c: node, actor + 0x30: driver, node + 0x1d4: ref,
                ref + 0x1c: state, ctx + 0x74: node}.items():
            m.put(address, value)
        for address, value in [(ctx + 0x44, relative), (ctx + 0x50, gap),
                (ctx + 0x14, 2), (ctx + 0x4c, 2),
                *[(state + 0x40 + 4 * j, v) for j, v in enumerate(xyz)]]:
            m.put(address, value, 'f')
        hits = []
        def observe(uc, address, size, data):
            hits.append(address)
            if address == 0x44fa9b:
                m.finished = True
                uc.emu_stop()
        for where in (0x44fa9b, 0x44fadd):
            m.uc.hook_add(UC_HOOK_CODE, observe, begin=where, end=where)
        m.run(0x44f8a0, (out, out + 0x10, out + 0x20, out + 0x24, out + 0x28), eax=ctx)
        if m.events or hits not in ([0x44fa9b], [0x44fadd]):
            raise ValueError('unexpected semantic hook or admission path')
        admitted = hits == [0x44fa9b]
        time = m.get(out + 0x20, 'f')
        rows.append(dict(position=xyz, relative=relative, gap=gap, admitted=admitted, time=time))
        lines.append('{{%s},%s,%s,%s,%s},' % (','.join(literal(v) for v in xyz),
            literal(relative), literal(gap), str(admitted).lower(), literal(time)))
    args.output.write_text('\n'.join(lines) + '\n', encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,
        fpu_control_word='007F', semantic_hooks=0, cases=rows), indent=2) + '\n', encoding='ascii')
    print('Original44F8A0 admission cases:', len(rows), 'semantic hooks: 0')


if __name__ == '__main__':
    main()
