"""Authenticated isolated x86 contact evidence, never a running-game capture.

Python 3.8+, Capstone 5.0.7, Unicorn 2.1.4; retail stays read-only.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import uuid

from collision256_trace import output_path
from world_body_trace import Image, INPUTS, digest_file, manifest


TOOLS = ('contact256_trace.py', 'contact256_native.py', 'contact256_test.py',
         'physics256_native.py', 'world_body_trace.py', 'collision256_trace.py')
ASSERTIONS = {
    0x4727CF: ('mov', 'dword ptr [ecx + 0x244], eax'),
    0x4727D5: ('mov', 'dword ptr [ecx + 0x248], edx'),
    0x472886: ('mov', 'byte ptr [esi + 0x240], 1'),
    0x4729F1: ('call', '0x471de0'),
    0x472A33: ('fld', 'dword ptr [edi + 0x10]'),
    0x472A4F: ('mov', 'byte ptr [edi + 0x15], al'),
    0x472A67: ('mov', 'byte ptr [edi + 0x14], al'),
    0x46BB02: ('call', '0x46b8c0'),
    0x46BD98: ('je', '0x46c612'),
    0x46C3EE: ('call', '0x465830'),
    0x4658B0: ('call', '0x465d00'),
    0x465EB4: ('mov', 'dword ptr [ecx + 0x10], esi'),
    0x465EC0: ('mov', 'dword ptr [eax + 0x38], esi'),
    0x466094: ('call', 'dword ptr [eax + 0x4c]'),
    0x466283: ('call', 'dword ptr [eax + 0x4c]'),
    0x46C5D2: ('call', 'dword ptr [edx + 0x4c]'),
    0x46C5E4: ('call', 'dword ptr [edx + 0x4c]'),
    0x46A313: ('call', '0x584f60'),
    0x584FD2: ('fstp', 'dword ptr [ecx + 0x70]'),
    0x58507A: ('fstp', 'dword ptr [ecx + 0x80]'),
    0x473CC7: ('mov', 'dword ptr [esi + 8], 0x473dd0'),
    0x473CCE: ('mov', 'dword ptr [esi + 0xc], 0x473d60'),
    0x5B0660: ('cmp', 'dword ptr [ebp + 8], 0'),
    0x5B066B: ('mov', 'esi, 0x6d4be0'),
    0x5B0670: ('mov', 'edi, 0x6d4ac0'),
}
SLOTS = {0x08: 0x46A1C0, 0x38: 0x469470, 0x48: 0x466840,
         0x4C: 0x46A260, 0x58: 0x46A080, 0x5C: 0x46A0C0,
         0x6C: 0x466900, 0x70: 0x466910}
CONSTANTS = (0x6CC8BC, 0x6B71E4, 0x6B71E8, 0x6CCAE0,
             0x6B727C, 0x6B7280, 0x6B72BC, 0x6B5B4C, 0x6B5B50, 0x70009C)


def source(image):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    for at, expected in ASSERTIONS.items():
        instructions = list(decoder.disasm(image.read(at, 16), at, count=1))
        if not instructions or (instructions[0].mnemonic, instructions[0].op_str) != expected:
            raise ValueError('source assertion failed: %08X' % at)
    for slot, target in SLOTS.items():
        if struct.unpack('<I', image.read(0x6C8798 + slot, 4))[0] != target:
            raise ValueError('car vtable slot failed: %02X' % slot)
    return dict(assertions={'%08X' % a: list(v) for a, v in ASSERTIONS.items()},
                car_vtable='006C8798', slots={'%02X' % a: '%08X' % v for a, v in SLOTS.items()},
                constants={'%08X' % a: struct.unpack('<f', image.read(a, 4))[0] for a in CONSTANTS})


def execution_source(image, native):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    executed = {}
    for record in native['cases'] + [native['cache']]:
        executed.update(record.pop('executed'))
    lines, blocks = [], []
    for address, size in sorted(executed.items()):
        raw = image.read(address, size)
        insns = list(decoder.disasm(raw, address))
        if len(insns) != 1 or insns[0].size != size:
            raise ValueError('executed instruction boundary mismatch')
        i = insns[0]
        lines.append('%08X %-8s %s' % (address, i.mnemonic, i.op_str))
        if blocks and blocks[-1][1] == address:
            blocks[-1][1] += size
        else:
            blocks.append([address, address + size])
    return '\n'.join(lines) + '\n', [dict(start='%08X' % a, end='%08X' % b,
        sha256=hashlib.sha256(image.read(a, b-a)).hexdigest().upper()) for a, b in blocks]


def guard_cases(image):
    from contact256_native import fixture
    guards = []
    class ChangedRead:
        def read(self, at, size):
            result = bytearray(image.read(at, size))
            if at <= 0x4727D3 < at + size:
                result[0x4727D3 - at] ^= 1
            return bytes(result)
    try:
        source(ChangedRead())
    except ValueError as error:
        if str(error) != 'source assertion failed: 004727CF':
            raise
        guards.append('changed ownership source rejected')
    else:
        raise AssertionError('changed source accepted')
    m = fixture(image, (8, 0, 0), (0, 0, 0), (0, 0, 0))
    try:
        m.run(0x5B065B, (0,), budget=1)
    except RuntimeError as error:
        if str(error) != 'instruction budget exhausted':
            raise
        guards.append('incomplete return rejected')
    else:
        raise AssertionError('incomplete return accepted')
    return guards


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    workspace, root = Path.cwd().resolve(), args.root.resolve()
    out = output_path(workspace, root, args.out)
    if not root.is_dir():
        p.error('missing retail root')
    for path in [root] + list(root.rglob('*')):
        if path.is_symlink() or getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError('retail reparse point is unsupported')
    before = manifest(root)
    out.mkdir(parents=True, exist_ok=False)
    (out / 'before.json').write_bytes(before)
    try:
        size, sha = INPUTS['speed.exe']
        if (root / 'speed.exe').stat().st_size != size or digest_file(root / 'speed.exe') != sha:
            raise ValueError('unsupported speed.exe identity')
        import capstone
        import unicorn
        if (capstone.__version__, unicorn.__version__) != ('5.0.7', '2.1.4'):
            raise ValueError('use pinned Capstone5.0.7 and Unicorn2.1.4')
        image = Image((root / 'speed.exe').read_bytes())
        facts = source(image)
        from contact256_native import suite
        native = suite(image)
        lines, blocks = execution_source(image, native)
        report = dict(schema='contact256-v1', run_id=str(uuid.uuid4()),
            input=dict(size=size, sha256=sha), source=facts, executed_blocks=blocks,
            native=native, guards=guard_cases(image),
            evidence='original-x86-emulation-synthetic-memory',
            runtime_capture=None, cadence=None, final_published_kinematics=None,
            versions=dict(capstone=capstone.__version__, unicorn=unicorn.__version__),
            tool_hashes={name: digest_file(Path(__file__).with_name(name)) for name in TOOLS})
        from contact256_test import validate
        validate(report)
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
    # No successful report is left by a crash, missing output or changed retail.
    (out / 'source.txt').write_text(lines, encoding='ascii')
    report['source_sha256'] = digest_file(out / 'source.txt')
    report['retail_manifest_sha256'] = hashlib.sha256(before).hexdigest().upper()
    (out / 'report.json').write_text(json.dumps(report, ensure_ascii=True,
        allow_nan=False, indent=2) + '\n', encoding='ascii')
    print('PASS: authenticated PE and source-addressed original execution')
    print('PASS: contact matrix, paired state, cache, callback and impulse assertions')
    print('PASS: source mismatch and incomplete-return guards')
    print('PASS: full retail manifests identical; fresh report completed')
    print('UNAVAILABLE: live event0, scheduler admission/order, published kinematics and cadence')


if __name__ == '__main__':
    main()
