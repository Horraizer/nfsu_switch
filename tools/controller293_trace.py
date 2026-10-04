"""Authenticated issue293 AI controls research. Standalone Python3.8+.

No game launch, attachment, patch or asset export. Outputs are research only.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from collision256_trace import output_path
from controller293_native import EXEC_RANGES, suite
from controller293_strategy import RANGES as STRATEGY_RANGES, suite as strategy_suite
from world_body_trace import Image, INPUTS, digest_file, manifest
from controller293_planner import suite as planner_suite, inputs as planner_inputs
from controller293_planner_ranges import RANGES as PLANNER_RANGES
from controller293_scene_ranges import RANGES as SCENE_RANGES
from controller293_event_rides_ranges import RANGES as RIDE_RANGES
from controller293_scene import suite as scene_suite

EXTRA_RANGES = STRATEGY_RANGES + PLANNER_RANGES + SCENE_RANGES + RIDE_RANGES + [
    ('actor-config-ride-ownership', 0x425374, 0x42538F),
    ('actor-state-construction', 0x42538F, 0x4253A9),
    ('actor-physical-binding', 0x4255AB, 0x4255CB),
    ('actor-world-binding', 0x425703, 0x42571B),
    ('action17-constructor', 0x453730, 0x453754),
    ('action17-exit', 0x453830, 0x45385D),
    ('controller-constructor', 0x4620C0, 0x462228),
    ('controller-update', 0x462430, 0x4624C3),
    ('controller-controls', 0x44C550, 0x44C85E),
    ('catch-up-scale', 0x44BD10, 0x44C00B),
    ('catch-up-comparison', 0x44C010, 0x44C24A),
    ('driver-constructor', 0x45DBB0, 0x45E056),
    ('driver-path-install', 0x45FAF0, 0x45FB27),
    ('path-resample', 0x459980, 0x459ABA),
    ('driver-id', 0x4600F0, 0x4600FB),
    ('config-context', 0x44CA10, 0x44CA1E),
    ('route-byte-index', 0x59BB80, 0x59BBAC),
    ('route-point-address', 0x442110, 0x442131),
    ('controller-route-reset', 0x44C250, 0x44C2DD),
    ('route-speed-index-slice', 0x44C68B, 0x44C73F),
    ('event-ai-scalar-consumer-slice', 0x43ECDB, 0x43ED3E),
]

ASSERTIONS = {
    0x42537D: ('add', 'edi, 0x10'),
    0x57C8C8: ('mov', 'dword ptr [0x734588], edi'),
    0x4B2A5D: ('mov', 'dword ptr [eax + ebx*4 + 0x44c], esi'),
    0x57F768: ('rep movsd', 'dword ptr es:[edi], dword ptr [esi]'),
    0x4B6100: ('rep stosd', 'dword ptr es:[edi], eax'),
    0x4257F7: ('mov', 'byte ptr [ebx + 0x4d0], 1'),
    0x57A310: ('mov', 'cl, byte ptr [eax + 0x4d0]'),
    0x57A318: ('je', '0x57a320'),
    0x41F889: ('mov', 'dword ptr [esi + 0x2d0], edx'),
    0x4B5E36: ('mov', 'dword ptr [0x78a320], 1'),
    0x430795: ('sub', 'ecx, eax'),
    0x5809E3: ('mov', 'byte ptr [esi + 3], cl'),
    0x4253A4: ('call', '0x464860'),
    0x4255C8: ('mov', 'dword ptr [ebx + 0x20], esi'),
    0x425715: ('mov', 'dword ptr [ebx + 0x82c], eax'),
    0x44204F: ('mov', 'dword ptr [0x736168], ecx'),
    0x44851F: ('mov', 'dword ptr [0x736174], ecx'),
    0x45032D: ('rep movsd', 'dword ptr es:[edi], dword ptr [esi]'),
    0x44B83F: ('call', 'dword ptr [eax + 0x60]'),
    0x44B854: ('call', 'dword ptr [edx + 0x18]'),
    0x44B857: ('mov', 'dword ptr [ebx + 0x3f8], eax'),
    0x4623D8: ('mov', 'edi, ecx'),
    0x4623EF: ('jne', '0x462403'),
    0x462403: ('push', '1'),
    0x462418: ('push', '3'),
    0x462421: ('push', '2'),
    0x44AA38: ('jmp', 'dword ptr [eax*4 + 0x44ab34]'),
    0x44AA44: ('call', '0x567160'),
    0x44AA56: ('call', '0x44a150'),
    0x44AA5B: ('mov', 'dword ptr [edi + 0x3fc], eax'),
    0x44A164: ('mov', 'dword ptr [esi], 0x6c98d0'),
    0x44A1A4: ('mov', 'dword ptr [esi], 0x6c98b0'),
    0x44A1D4: ('mov', 'dword ptr [esi], 0x6c97fc'),
    0x44A1F0: ('fld', 'dword ptr [0x6cc98c]'),
    0x44DCF8: ('push', '7'),
    0x44DCFF: ('mov', 'dword ptr [esi], 0x6c9374'),
    0x44E278: ('push', '8'),
    0x44E27F: ('mov', 'dword ptr [esi], 0x6c920c'),
    0x44E2F4: ('mov', 'byte ptr [esi + 0x1f0], al'),
    0x44E2E0: ('rep stosd', 'dword ptr es:[edi], eax'),
    0x450257: ('jne', '0x45033f'),
    0x4502D7: ('call', '0x45b050'),
    0x56719F: ('mov', 'dword ptr [esi + 0x10], ecx'),
    0x453736: ('push', '0x11'),
    0x45373F: ('mov', 'dword ptr [esi], 0x6c92d4'),
    0x453836: ('mov', 'eax, dword ptr [eax + 0x30]'),
    0x45384C: ('mov', 'byte ptr [eax + 0x220], dl'),
    0x4620E5: ('mov', 'dword ptr [esi], 0x6c89f0'),
    0x46219D: ('call', '0x45dbb0'),
    0x4621A6: ('mov', 'dword ptr [esi + 8], eax'),
    0x4624B8: ('call', '0x44b1b0'),
    0x45DC13: ('mov', 'byte ptr [ebp + 0x20c], 1'),
    0x45E188: ('mov', 'dword ptr [ecx + 0x200], eax'),
    0x45E19C: ('mov', 'dword ptr [ecx + 0x208], eax'),
    0x45E1A2: ('mov', 'byte ptr [ecx + 0x20c], dl'),
    0x44C56E: ('call', 'dword ptr [eax + 0x3c]'),
    0x44C573: ('call', '0x44bd10'),
    0x44C588: ('cmp', 'dword ptr [esi], 0'),
    0x44C5B8: ('mov', 'cl, byte ptr [eax + 0x1f0]'),
    0x44C5F7: ('movzx', 'eax, byte ptr [eax + edx + 0x6a]'),
    0x44C6CD: ('imul', 'edi, edi, 0x88'),
    0x44C6D9: ('movzx', 'eax, byte ptr [edi + edx + 0x2e6]'),
    0x44C72B: ('fmul', 'dword ptr [eax + 0x77a00c]'),
    0x44C731: ('fmul', 'dword ptr [ebx + 0x4b8]'),
    0x44C745: ('mov', 'ecx, dword ptr [eax + 0x1e4]'),
    0x44C7CB: ('jp', '0x44c7d3'),
    0x44C7EE: ('call', 'dword ptr [eax + 0x38]'),
    0x44BFFE: ('mov', 'dword ptr [edi + 0x4b8], ecx'),
    0x44CA16: ('movsx', 'eax, byte ptr [ecx + 0x735]'),
    0x43ED1A: ('fmul', 'dword ptr [0x78a380]'),
    0x45FBB1: ('call', '0x583ea0'),
    0x45FBD3: ('movsx', 'ecx, byte ptr [edi + 0x424]'),
    0x45FBEB: ('call', '0x583ea0'),
    0x45FC5A: ('call', '0x4590a0'),
    0x45FC67: ('fstp', 'dword ptr [ebx + 0x1f8]'),
    0x45FC6D: ('mov', 'dword ptr [ebx + 0x1f4], ecx'),
    0x45971E: ('cmp', 'eax, ecx'),
    0x45E39A: ('call', '0x564db0'),
    0x45E4A1: ('call', '0x4592f0'),
    0x45EAF3: ('call', '0x45e370'),
    0x45EB7E: ('je', '0x45f2b4'),
    0x45F3A4: ('fld', 'dword ptr [ebp + 0x200]'),
    0x45F3AC: ('fsub', 'dword ptr [eax + 0x3d4]'),
    0x45F583: ('call', '0x45fc90'),
    0x45F8DD: ('je', '0x45f8f9'),
    0x45FA53: ('mov', 'word ptr [ecx + 0x1d0], dx'),
    0x45FA5E: ('mov', 'dword ptr [edx + 0x1d4], ebp'),
    0x45FA7D: ('mov', 'dword ptr [ecx + 0x1e4], eax'),
    0x45FA97: ('mov', 'dword ptr [edx + 0x1dc], edi'),
    0x45FD81: ('fld', 'dword ptr [edi + 0x200]'),
    0x45FE1F: ('call', '0x583ea0'),
    0x45FE50: ('fld', 'dword ptr [edi + 0x1dc]'),
    0x45FE56: ('fsub', 'dword ptr [edi + 0x1e4]'),
    0x45FF6E: ('mov', 'dword ptr [ebp], 0'),
    0x45FF75: ('mov', 'dword ptr [ecx], eax'),
    0x460033: ('mov', 'ecx, dword ptr [esp + 0x1c]'),
    0x689FDC: ('fstp', 'dword ptr [0x77a4a8]'),
    0x689FF0: ('mov', 'dword ptr [0x77a4a4], 0x3dd67750'),
}

TABLES = {
    'IsBadReadPtr-IAT': (0x697198, 0x2D2926),
    'IsBadWritePtr-IAT': (0x69719C, 0x2D2936),
    'strategy-selection': (0x6C8A50, 0x4623D0),
    'strategy1-cap': (0x6C98E0, 0x44A1F0),
    'strategy2-cap': (0x6C98C0, 0x44A1F0),
    'strategy3-cap': (0x6C980C, 0x44A1F0),
    'strategy1-first-action': (0x6C98E8, 0x44DCE0),
    'strategy2-first-action': (0x6C98C8, 0x44E260),
    'strategy3-first-action': (0x6C9814, 0x44E260),
    'action7-update': (0x6C937C, 0x450210),
    'action17-exit': (0x6C92E0, 0x453830),
    'controller-update': (0x6C89FC, 0x462430),
    'context-accessor': (0x6C8A48, 0x44CA10),
    'driver-update': (0x6C8C1C, 0x45E970),
    'input-setter': (0x6C8C50, 0x45E180),
    'path-install': (0x6C8C54, 0x45FAF0),
    'steering-rebase': (0x6C8C74, 0x45E650),
    'speed-rebase': (0x6C8C84, 0x45E750),
    'actor-id': (0x6C8C5C, 0x4600F0),
}

TOOL_FILES = ('controller293_trace.py', 'controller293_native.py', 'controller293_test.py',
              'controller293_strategy.py', 'controller293_planner.py',
              'controller293_planner_ranges.py',
              'controller293_scene.py', 'controller293_scene_ranges.py', 'event0_trace.py',
              'controller293_event_rides.py', 'controller293_event_rides_ranges.py',
              'physics256_native.py', 'collision256_trace.py', 'world_body_trace.py')


def source(image):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoded, spans, lines = {}, [], []
    ranges = [('execution-%08X' % start, start, end) for start, end in EXEC_RANGES] + EXTRA_RANGES
    for name, start, end in ranges:
        raw = image.read(start, end - start)
        instructions = list(md.disasm(raw, start))
        if sum(x.size for x in instructions) != len(raw):
            raise ValueError('incomplete source range: ' + name)
        spans.append(dict(name=name, start='%08X' % start, end='%08X' % end,
                          sha256=hashlib.sha256(raw).hexdigest().upper()))
        lines.append('\n' + name)
        for insn in instructions:
            decoded[insn.address] = (insn.mnemonic, insn.op_str)
            lines.append('%08X %-8s %s' % (insn.address, insn.mnemonic, insn.op_str))
    for at, expected in ASSERTIONS.items():
        if decoded.get(at) != expected:
            raise ValueError('source assertion failed: %08X' % at)
    for name, (at, expected) in TABLES.items():
        if struct.unpack('<I', image.read(at, 4))[0] != expected:
            raise ValueError('source table failed: ' + name)
    for at, expected in ((0x6D2928, b'IsBadReadPtr\0'), (0x6D2938, b'IsBadWritePtr\0')):
        if image.read(at, len(expected)) != expected:
            raise ValueError('external pointer-probe import name changed')
    # Provenance hashes, not exported retail data. Includes actual curve samples,
    # descriptors, control gains, angle table, and source-backed startup inputs.
    data_spans = []
    for start, end in [(0x706098, 0x7061B8), (0x7003F8, 0x700620),
                       (0x6B73AC, 0x6B74AC), (0x6CC98C, 0x6CC990),
                       (0x44AB34, 0x44AB48), (0x6F2280, 0x6F2880)]:
        data_spans.append(dict(start='%08X' % start, end='%08X' % end,
                              sha256=hashlib.sha256(image.read(start, end-start)).hexdigest().upper()))
    return '\n'.join(lines) + '\n', dict(ranges=spans, data_ranges=data_spans,
        assertions={'%08X' % k: list(v) for k, v in ASSERTIONS.items()},
        tables={k: ['%08X' % a, '%08X' % v] for k, (a, v) in TABLES.items()})


def negative_source(image):
    results = []
    for address, byte, expected in [(0x45EB7F, 0x85, 'source assertion failed: 0045EB7E'),
                                    (0x44C7CB, 0x7B, 'source assertion failed: 0044C7CB'),
                                    (0x4623F0, 0x85, 'source assertion failed: 004623EF'),
                                    (0x57A318, 0x75, 'source assertion failed: 0057A318')]:
        class ChangedRead:
            def read(self, va, size):
                raw = bytearray(image.read(va, size))
                if va <= address < va + size:
                    raw[address-va] = byte
                return bytes(raw)
        try:
            source(ChangedRead())
        except ValueError as error:
            if str(error) != expected:
                raise
            results.append(str(error))
        else:
            raise AssertionError('wrong source branch accepted')
    return results


def main():
    import capstone
    import unicorn
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if (capstone.__version__, unicorn.__version__) != ('5.0.7', '2.1.4'):
        raise ValueError('use Capstone5.0.7 and Unicorn2.1.4')
    root = args.root.resolve()
    if not root.is_dir():
        raise ValueError('missing retail root')
    out = output_path(Path.cwd().resolve(), root, args.out)
    for path in [root] + list(root.rglob('*')):
        if getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError('retail reparse point is unsupported')
    before = manifest(root)
    out.mkdir(parents=True, exist_ok=False)
    (out / 'before.json').write_bytes(before)
    try:
        exe = root / 'speed.exe'
        size, sha = INPUTS['speed.exe']
        if (exe.stat().st_size, digest_file(exe)) != (size, sha):
            raise ValueError('unsupported speed.exe identity')
        image = Image(exe.read_bytes())
        lines, facts = source(image)
        report = dict(input=dict(size=size, sha256=sha), source=facts,
                      negative_source_guards=negative_source(image), native=suite(image),
                      strategy=strategy_suite(image),
                      planner=planner_suite(image, planner_inputs(root)),
                      scene=scene_suite(image, planner_inputs(root)),
                      event_scene=scene_suite(image, planner_inputs(root), event_rides=True),
                      versions=dict(capstone=capstone.__version__, unicorn=unicorn.__version__),
                      tool_hashes={n: digest_file(Path(__file__).with_name(n)) for n in TOOL_FILES},
                      contract='controller293-v5', original_runtime=None,
                      inherited_cpp_validated=False)
        (out / 'source.txt').write_text(lines, encoding='ascii')
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=True,
            allow_nan=False, indent=2) + '\n', encoding='ascii')
        print('PASS: authenticated controller source, tables and wrong-branch guards')
        print('PASS: bounded original-x86 controls captured; run controller293_test.py')
        print('PASS: retail-backed original planner, retained action and driver witnesses')
        print('PASS: original event metadata, paired context, history and catch-up witnesses')
        print('PASS: original event preset/adaptive RideInfo and paired actor bindings')
        print('UNAVAILABLE: actual event0 scene/trajectory, overtake/contact, full parity')
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('PASS: full retail manifests identical')


if __name__ == '__main__':
    main()
