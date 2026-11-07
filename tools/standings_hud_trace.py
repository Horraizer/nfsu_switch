"""Bounded read-only PC1.4 USA standings HUD extraction; requires Capstone.

No retail assets are copied. Reports contain object fields and disassembly.
Run from the checkout with --root <retail-root> --out <new-output-directory>.
"""
import argparse
import json
from pathlib import Path
import hud254_trace as hud


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents or out.exists():
        parser.error('output must be new and outside retail')
    paths = [root / 'speed.exe', root / 'Global/INGAMEB.BUN']
    inputs = [p.read_bytes() for p in paths]
    expected = [hud.EXE_SHA, hud.BUNDLE_SHA]
    if [hud.sha(data) for data in inputs] != expected:
        parser.error('requires authenticated PC1.4 USA inputs')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    pe = hud.PE(inputs[0])
    package = hud.jdlz(inputs[1][0x16F40C:])
    objects = hud.feng_objects(package, ('lb', 'leader', 'position', 'numberracers'))
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    lines = []
    # Separate entry points avoid decoding padding/jump tables as instructions.
    for a, b in [(0x4A00C0, 0x4A0199), (0x4A01A0, 0x4A03C2),
                 (0x4A2AC1, 0x4A2B0A), (0x4A2B10, 0x4A2EED),
                 (0x4A2EF0, 0x4A301E), (0x4A31E0, 0x4A33EF),
                 (0x4A3930, 0x4A3A97), (0x4A3AA0, 0x4A3B98),
                 (0x434ED0, 0x434F80), (0x433340, 0x4333B1),
                 (0x4353E0, 0x435566), (0x42E945, 0x42E98C)]:
        lines.append('RANGE %08X %08X' % (a, b))
        lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str)
                     for i in md.disasm(pe.read(a, b-a), a))
    out.mkdir(parents=True)
    (out / 'objects.json').write_text(json.dumps(objects, indent=2), encoding='ascii')
    (out / 'instructions.txt').write_text('\n'.join(lines)+'\n', encoding='ascii')
    after = [hud.sha(p.read_bytes()) for p in paths]
    if after != expected:
        raise RuntimeError('retail input changed')
    (out / 'inputs.json').write_text(json.dumps(dict(
        inputs=[dict(path=str(p), sha256=s) for p, s in zip(paths, after)],
        readonly=True), indent=2), encoding='ascii')
    print('PASS authenticated standings layout and bounded source extraction')


if __name__ == '__main__':
    main()
