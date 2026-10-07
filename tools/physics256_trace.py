"""Issue256 driver/physics/phase trace; authenticated PE, read-only retail.

Python3.8+, Capstone5.0.7, Unicorn2.1.4. Original runtime is unavailable.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from collision256_trace import output_path, source_trace as prior_trace
from world_body_trace import Image, INPUTS, digest_file, manifest


RANGES = [
    ('human-driver', 0x4615E0, 0x4617C9),
    ('ai-driver', 0x45E970, 0x45FAEC),
    ('physics-selection', 0x425150, 0x425195),
    ('physics-replacement-slice', 0x426144, 0x426296),
    ('physics1-constructor-slice', 0x476840, 0x4768AF),
    ('physics3-constructor-slice', 0x4740D0, 0x474138),
    ('body-constructor-slice', 0x469B00, 0x469BBA),
    ('body-step', 0x46A030, 0x46A060),
    ('body-snapshot', 0x468F70, 0x468FD4),
    ('actor-physics-wrapper', 0x4264D0, 0x42679D),
    ('physics1-step', 0x4774A0, 0x4782B6),
    ('physics3-step', 0x474A90, 0x474C99),
    ('float-integer-conversion', 0x674898, 0x67490D),
    ('physics1-publication-slice', 0x4789D0, 0x478B02),
    ('physics3-publication-slice', 0x474730, 0x47485F),
    ('actor-pre', 0x4267A0, 0x42692F),
    ('actor-post-derivatives-slice', 0x426930, 0x426A5C),
    ('world-driver-pre-contact-post', 0x420D96, 0x420E50),
    ('group-scheduler', 0x420890, 0x420B3D),
    ('group-unique-advance', 0x592CB0, 0x592D4C),
    ('group-time-advance', 0x592D50, 0x592E75),
    ('body-list-end', 0x40CB70, 0x40CB7C),
    ('physics3-query', 0x4740C0, 0x4740C6),
    ('driver-control-accessors', 0x424FF0, 0x425030),
]

ASSERTIONS = {
    0x42614A: ('mov', 'dword ptr [esi + 0x28], edi'),
    0x426153: ('jmp', 'dword ptr [edi*4 + 0x4262a8]'),
    0x4261B6: ('call', '0x476840'),
    0x4261C5: ('mov', 'dword ptr [esi + 0x34], eax'),
    0x476882: ('mov', 'dword ptr [ebp], 0x6c8364'),
    0x476898: ('mov', 'dword ptr [ebp + 0x5c], ecx'),
    0x4768A9: ('mov', 'dword ptr [ebp + 4], edx'),
    0x474127: ('mov', 'dword ptr [ebp + 0x34], eax'),
    0x474135: ('mov', 'dword ptr [ebp + 0x30], edi'),
    0x469B74: ('mov', 'dword ptr [edi], 0x6c8798'),
    0x469BAA: ('mov', 'dword ptr [edi + 0x140], ebp'),
    0x46A03E: ('mov', 'eax, dword ptr [esi + 0x140]'),
    0x46A046: ('call', '0x4264d0'),
    0x4264EB: ('mov', 'ecx, dword ptr [eax + 0x34]'),
    0x4264EE: ('mov', 'eax, dword ptr [esp + 0x20]'),
    0x4264F5: ('call', 'dword ptr [edx + 4]'),
    0x45F538: ('mov', 'ecx, dword ptr [eax + 0x34]'),
    0x45F550: ('call', 'dword ptr [edx + 0x10]'),
    0x477518: ('jne', '0x477d56'),
    0x477D59: ('fdiv', 'dword ptr [0x6ffe14]'),
    0x477DED: ('call', '0x46aa40'),
    0x4781B9: ('jne', '0x477d90'),
    0x478257: ('call', '0x4789d0'),
    0x474BE7: ('fdiv', 'dword ptr [0x6fff10]'),
    0x474C2D: ('call', '0x46aa40'),
    0x474C53: ('call', '0x474730'),
    0x592DA4: ('call', '0x592cb0'),
    0x592DB2: ('call', '0x592fd0'),
    0x592E64: ('call', 'dword ptr [eax + 0x28]'),
    0x4267C4: ('mov', 'dword ptr [esi + 0x490], eax'),
    0x4267E1: ('mov', 'dword ptr [esi + 0x4a0], ecx'),
    0x426998: ('fld', 'dword ptr [0x6cc7bc]'),
    0x42699E: ('fdiv', 'dword ptr [ebp + 8]'),
}
TABLES = {
    'selector-jump-table': (0x4262A8, [0x42615A, 0x426189, 0x4261DA, 0x42620A, 0x426255]),
    'physics1-step': (0x6C8368, [0x4774A0]),
    'physics3-step': (0x6C846C, [0x474A90]),
    'body-step': (0x6C87C0, [0x46A030]),
}


def source(image):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    lines, decoded, ranges = [], {}, []
    for name, start, end in RANGES:
        raw = image.read(start, end - start)
        insns = list(md.disasm(raw, start))
        if sum(i.size for i in insns) != len(raw):
            raise ValueError('incomplete source range: ' + name)
        ranges.append(dict(name=name, start='%08X' % start, end='%08X' % end,
                           sha256=hashlib.sha256(raw).hexdigest().upper()))
        lines.append('\n' + name)
        for insn in insns:
            decoded[insn.address] = insn
            lines.append('%08X %-8s %s' % (insn.address, insn.mnemonic, insn.op_str))
    for at, expected in ASSERTIONS.items():
        insn = decoded.get(at)
        if insn is None or (insn.mnemonic, insn.op_str) != expected:
            raise ValueError('source assertion failed: %08X' % at)
    tables = {}
    for name, (at, expected) in TABLES.items():
        actual = list(struct.unpack('<' + 'I' * len(expected), image.read(at, len(expected) * 4)))
        if actual != expected:
            raise ValueError('source table failed: ' + name)
        tables[name] = dict(address='%08X' % at, values=['%08X' % x for x in actual])
    constants = {('%08X' % at): struct.unpack('<f', image.read(at, 4))[0]
                 for at in (0x6FFE14, 0x6FFF10, 0x6EF2C8, 0x6CCAB0)}
    return '\n'.join(lines) + '\n', dict(ranges=ranges, tables=tables,
        assertions={('%08X' % k): list(v) for k, v in ASSERTIONS.items()},
        constants=constants,
        phase_contract='physics256-v1',
        direct_driver_integration_edge=None,
        recovered_physics_chain=['body+0x28 -> 0046A030', '004264D0',
                                 'actor+0x34 virtual+4', 'selector1:004774A0;selector3:00474A90'],
        original_runtime=None, original_cadence=None, original_contact_matrix=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    root, workspace = args.root.resolve(), Path.cwd().resolve()
    out = output_path(workspace, root, args.out)
    if not root.is_dir():
        parser.error('missing retail root')
    # Reject Windows junctions as well as symbolic links before following paths.
    for path in [root] + list(root.rglob('*')):
        if getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError('retail reparse point is unsupported')
    before = manifest(root)
    out.mkdir(parents=True, exist_ok=False)
    (out / 'before.json').write_bytes(before)
    try:
        size, sha = INPUTS['speed.exe']
        exe = root / 'speed.exe'
        if exe.stat().st_size != size or digest_file(exe) != sha:
            raise ValueError('unsupported speed.exe identity')
        image = Image(exe.read_bytes())
        previous_lines, previous = prior_trace(image)
        lines, facts = source(image)
        negative_guards = []
        # Change only memory presented to this decoder, never the retail file.
        for address, replacement, expected in (
                (0x4264ED, b'\x30', 'source assertion failed: 004264EB'),
                (0x6C8368, struct.pack('<I', 0x474A90), 'source table failed: physics1-step')):
            class ChangedRead:
                def read(self, va, size):
                    value = bytearray(image.read(va, size))
                    for i, byte in enumerate(replacement):
                        if va <= address + i < va + size:
                            value[address + i - va] = byte
                    return bytes(value)
            try:
                source(ChangedRead())
            except ValueError as error:
                if str(error) != expected:
                    raise
                negative_guards.append(str(error))
            else:
                raise AssertionError('changed source unexpectedly accepted')
        from physics256_native import suite
        import capstone
        import unicorn
        if (capstone.__version__, unicorn.__version__) != ('5.0.7', '2.1.4'):
            raise ValueError('use pinned Capstone5.0.7 and Unicorn2.1.4')
        native = suite(image)
        report = dict(input=dict(size=size, sha256=sha), source=facts,
                      ownership=previous['ownership_instructions'],
                      native=native, versions=dict(capstone=capstone.__version__,
                                                   unicorn=unicorn.__version__),
                      negative_source_guards=negative_guards,
                      tool_hashes={name: digest_file(Path(__file__).with_name(name))
                                   for name in ('physics256_trace.py', 'physics256_native.py')})
        (out / 'source.txt').write_text(previous_lines + lines, encoding='ascii')
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=True,
            allow_nan=False, indent=2) + '\n', encoding='ascii')
        print('PASS: authenticated PE, ownership, exact instruction boundaries and tables')
        print('PASS: bounded emulated call captures completed; run physics256_test.py for assertions')
        print('UNAVAILABLE: original runtime cadence, contacts and full physics parity')
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('PASS: full retail manifests identical')


if __name__ == '__main__':
    main()
