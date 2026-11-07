"""Bounded issue 256 static evidence; Python 3.8+ and Capstone.

No game is launched, attached, patched or emulated. PASS authenticates the
input files and this static reproduction, never original runtime behavior.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from world_body_trace import Image, INPUTS, chunks, digest_file, jdlz, manifest


# Exclusive ends at decoded instruction boundaries. Slices are named as such.
# Do not sweep through inter-function padding to discover supposed callers.
RANGES = [
    ('world-serialization', 0x41FC00, 0x41FE4D),
    ('serialization-caller-slice', 0x421E80, 0x421EA9),
    ('serialization-read-bytes', 0x571BA0, 0x571C16),
    ('serialization-direction-slice', 0x571C20, 0x571C99),
    ('world-driver-and-collision-dispatch-slice', 0x420D96, 0x420E56),
    ('frame-world-call-slice', 0x447924, 0x44797F),
    ('frame-clock-call-slice', 0x447A10, 0x447A2F),
    ('clock-accounting', 0x585D70, 0x585DF6),
    ('controller-driver-alias-slice', 0x4260D4, 0x4260F6),
    ('physics-construction-and-store-slice', 0x42620A, 0x426244),
    ('physics-driver-relink-slice', 0x42628C, 0x426296),
    ('ai-driver-vtable-install-slice', 0x45DBB0, 0x45DBF0),
    ('real-driver-vtable-install-slice', 0x460E70, 0x460E91),
    ('physics-constructor-slice', 0x4740D0, 0x474138),
    ('physics-integration-dispatch-untraced', 0x474A90, 0x474C99),
    ('body-refresh-pass', 0x420310, 0x42037F),
    ('queue-scheduler', 0x420380, 0x42088D),
    ('group-scheduler', 0x420890, 0x420B3D),
    ('registered-pair-builder', 0x469580, 0x46976A),
    ('pair-constructor-slice', 0x466CE0, 0x466D6B),
    ('query-and-enqueue', 0x468060, 0x4682BD),
    ('job-comparator', 0x46ADC0, 0x46AE3A),
    ('job-constructor', 0x46AE40, 0x46AE5E),
    ('job-time-search', 0x46AE60, 0x46B067),
    ('sorted-list-insert', 0x564940, 0x56498C),
    ('queued-job-response', 0x46B5E0, 0x46B783),
    ('group-job-response', 0x46B790, 0x46B8B5),
    ('invalidate-affected-jobs', 0x46AC90, 0x46ACD1),
    ('contact-point-and-normal', 0x46B8C0, 0x46BA4D),
    ('polygon-normal-orientation', 0x46B070, 0x46B25D),
    ('solver', 0x46BAE0, 0x46C61B),
    ('origin-comparison', 0x471D60, 0x471DA6),
    ('witness-update', 0x472830, 0x47294A),
    ('witness-query', 0x472950, 0x472A83),
    ('geometric-query', 0x471DE0, 0x472391),
    ('current-hit-and-accumulation-slice', 0x465830, 0x4658B5),
    ('group-pair-scan', 0x592C50, 0x592CA4),
    ('group-unique-body-update', 0x592CB0, 0x592D4C),
    ('group-time-advance', 0x592D50, 0x592E75),
    ('group-next-hit-search', 0x592E80, 0x592FCE),
    ('group-resolve-and-requeue', 0x592FD0, 0x5930FC),
    ('group-time-order', 0x593100, 0x593169),
    ('shape-loader', 0x472B00, 0x472BF5),
    ('shape-forward-lookup', 0x472C60, 0x472CA9),
    ('shape-pivot-adjustment', 0x472E40, 0x472E9C),
    ('shape-point-reader', 0x472EE0, 0x472F2C),
    ('definition-tail-insertion-slice', 0x5952A0, 0x595300),
    ('virgin-definition-and-placement', 0x5962E0, 0x596486),
    ('smokeable-shape-and-thresholds', 0x5968A0, 0x5969FC),
    ('prop-impact-activation', 0x597980, 0x597B7D),
]

# Instruction boundaries and immediate calls are checked independently of
# range lengths. Hash identity does not make a misaligned decode meaningful.
CALLS = {
    0x421E97: 0x41FC00, 0x447969: 0x420B40,
    0x426230: 0x4740D0,
    0x420E18: 0x420890, 0x420E1F: 0x420310, 0x420E26: 0x420380,
    0x474C2D: 0x46AA40, 0x420496: 0x469580,
    0x4204ED: 0x468060, 0x42054A: 0x46B5E0,
    0x420689: 0x468060, 0x46814D: 0x472830,
    0x468154: 0x472950, 0x4682A4: 0x564940,
    0x46AFCD: 0x472830, 0x46AFD5: 0x472950,
    0x46B750: 0x46BAE0, 0x46B89C: 0x46BAE0,
    0x46C3EE: 0x465830, 0x4209BE: 0x592C50,
    0x593007: 0x46B790,
}

# A physics vtable slot alone cannot identify the world's driver receiver.
# Check the receiver, alias stores, distinct physics store and installations.
OWNERSHIP_INSTRUCTIONS = {
    0x420DA9: ('mov', 'ecx, dword ptr [ebx + 0x30]'),
    0x420DAF: ('mov', 'eax, dword ptr [ecx]'),
    0x420DB2: ('call', 'dword ptr [eax + 4]'),
    0x4260D4: ('mov', 'dword ptr [esi + 0x2c], eax'),
    0x4260D7: ('mov', 'ecx, dword ptr [esi + 0x2c]'),
    0x4260DA: ('mov', 'eax, dword ptr [ecx + 8]'),
    0x4260DD: ('mov', 'edx, dword ptr [esi + 0x34]'),
    0x4260E4: ('mov', 'dword ptr [esi + 0x30], eax'),
    0x4260E7: ('mov', 'dword ptr [edx + 4], eax'),
    0x426235: ('mov', 'dword ptr [esi + 0x34], eax'),
    0x42628C: ('mov', 'edx, dword ptr [esi + 0x34]'),
    0x42628F: ('mov', 'eax, dword ptr [esi + 0x30]'),
    0x426292: ('mov', 'dword ptr [edx + 4], eax'),
    0x45DBE9: ('mov', 'dword ptr [ebp], 0x6c8c18'),
    0x460E8B: ('mov', 'dword ptr [esi], 0x6c8ad0'),
    0x474110: ('mov', 'dword ptr [ebp], 0x6c8468'),
}
VTABLE_SLOTS = (
    ('ai-driver', 0x6C8C18, 4, 0x45E970),
    ('real-driver', 0x6C8AD0, 4, 0x4615E0),
    ('separate-physics', 0x6C8468, 4, 0x474A90),
)

CONSTANTS = (0x6FFF10, 0x6B71E4, 0x6B71E8, 0x6CC8BC,
             0x700E5C, 0x700E60, 0x700088, 0x6EF2C8, 0x6CCAE0)
STRINGS = (0x6CC0A0, 0x6C888C, 0x6C88A8, 0x6C8814, 0x6CAE64)


def source_trace(image):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    lines, ranges, decoded = [], [], {}
    for name, start, end in RANGES:
        raw = image.read(start, end - start)
        instructions = list(decoder.disasm(raw, start))
        if sum(i.size for i in instructions) != len(raw):
            raise ValueError('incomplete instruction range: ' + name)
        sha = hashlib.sha256(raw).hexdigest().upper()
        ranges.append(dict(name=name, start='%08X' % start,
                           end='%08X' % end, sha256=sha))
        lines.append('\n%s %08X..%08X SHA256 %s' % (name, start, end, sha))
        for insn in instructions:
            decoded[insn.address] = insn
            lines.append('%08X %-8s %s' % (insn.address, insn.mnemonic, insn.op_str))
    for site, target in CALLS.items():
        insn = decoded.get(site)
        if not insn or insn.mnemonic != 'call' or insn.op_str != hex(target):
            raise ValueError('call edge failed: %08X' % site)
    for site, expected in OWNERSHIP_INSTRUCTIONS.items():
        insn = decoded.get(site)
        if not insn or (insn.mnemonic, insn.op_str) != expected:
            raise ValueError('object ownership instruction failed: %08X' % site)
    slots = []
    for name, table, offset, target in VTABLE_SLOTS:
        if struct.unpack('<I', image.read(table + offset, 4))[0] != target:
            raise ValueError('vtable slot changed: ' + name)
        slots.append(dict(object=name, table='%08X' % table, offset=offset,
                          target='%08X' % target))
    constants = {}
    for va in CONSTANTS:
        raw = image.read(va, 4)
        constants['%08X' % va] = dict(f32=struct.unpack('<f', raw)[0],
                                     u32=struct.unpack('<I', raw)[0])
    strings = {('%08X' % va): image.read(va, 100).split(b'\0')[0].decode('ascii')
               for va in STRINGS}
    facts = dict(evidence='static-original-file', ranges=ranges,
                 calls={'%08X' % a: '%08X' % b for a, b in CALLS.items()},
                 ownership_instructions={'%08X' % a: list(b)
                                         for a, b in OWNERSHIP_INSTRUCTIONS.items()},
                 vtable_slots=slots,
                 world_driver_receiver='actor+0x30',
                 separate_physics_pointer='actor+0x34',
                 driver_to_physics_dispatch=None,
                 physics_placement_within_world_call=None,
                 runtime_driver_and_physics_targets=None,
                 constants=constants, serialization_labels=strings,
                 runtime_selector_00736200=None, runtime_cadence=None,
                 original_contact_matrix=None)
    return '\n'.join(lines) + '\n', facts


def aligned(offset, end, stride):
    start = (offset + 23) & ~15
    if start > end or (end - start) % stride:
        raise ValueError('partial aligned record array')
    return start


def floats(data, offset, count):
    values = struct.unpack_from('<' + 'f' * count, data, offset)
    if not all(math.isfinite(v) for v in values):
        raise ValueError('non-finite source field')
    return list(values)


def shape_rows(data, wanted):
    result = []
    for tag, offset, end in chunks(data):
        if tag != 0x80034020:
            continue
        # Match the loader's sequential three-chunk layout; do not merge by hash.
        children = list(chunks(data, offset + 8, end))
        for index, (child, at, finish) in enumerate(children):
            if child != 0x39200:
                continue
            header = aligned(at, finish, 1)
            if finish - header < 0x58:
                raise ValueError('short shape header')
            key = struct.unpack_from('<I', data, header + 0x18)[0]
            if key != wanted:
                continue
            if index + 2 >= len(children):
                raise ValueError('shape arrays missing')
            ptag, poff, pend = children[index + 1]
            itag, ioff, iend = children[index + 2]
            if (ptag, itag) != (0x39201, 0x39202):
                raise ValueError('unexpected shape array tags')
            points = aligned(poff, pend, 48)
            indices = aligned(ioff, iend, 2)
            # Metadata and hashes only: no exported hull vertices/asset bytes.
            result.append(dict(hash='%08X' % key, header_chunk=at, header_record=header,
                point_chunk=poff, point_start=points, point_records=(pend-points)//48,
                index_chunk=ioff, index_start=indices, index_records=(iend-indices)//2,
                sha256=hashlib.sha256(data[at:iend]).hexdigest().upper()))
    return result


def named_prop(data, stream):
    key = 0x5087DB01
    definitions, placements = [], []
    for tag, offset, end in chunks(data):
        if tag == 0x34026:
            for at in range(aligned(offset, end, 256), end, 256):
                if struct.unpack_from('<I', data, at + 0x80)[0] != key:
                    continue
                definitions.append(dict(offset=at,
                    name=data[at+8:at+0x20].split(b'\0')[0].decode('ascii'),
                    hash='%08X' % key, mass_field=floats(data, at+0x20, 1)[0],
                    pivot=floats(data, at+0xB0, 3),
                    activation_field=floats(data, at+0xC4, 1)[0],
                    break_field=floats(data, at+0xC8, 1)[0],
                    descriptor_sha256=hashlib.sha256(data[at+0x20:at+0x80]).hexdigest().upper()))
        elif tag == 0x34027:
            for at in range(aligned(offset, end, 112), end, 112):
                value, section, instance, model = struct.unpack_from('<IhhI', data, at)
                if value == key and (section, instance) in ((1204, 49), (1204, 50)):
                    placements.append(dict(offset=at, section=section, instance=instance,
                        model='%08X' % model, translation=floats(data, at+0x60, 3),
                        scenery=[]))
    for tag, offset, end in chunks(stream):
        if tag != 0x80034100:
            continue
        children = {t: (o, e) for t, o, e in chunks(stream, offset+8, end)}
        head, hend = children[0x34101]
        head = aligned(head, hend, 60)
        section = struct.unpack_from('<I', stream, head+12)[0]
        if section != 1204:
            continue
        inst, iend = children[0x34103]
        inst = aligned(inst, iend, 48)
        obj, oend = children[0x34102]
        for placed in placements:
            at = inst + 48 * placed['instance']
            if at + 48 > iend:
                raise ValueError('scenery instance outside array')
            obj_at = obj + 8 + 72 * struct.unpack_from('<H', stream, at+12)[0]
            if obj_at + 72 > oend:
                raise ValueError('scenery object outside array')
            models = ['%08X' % x for x in struct.unpack_from('<6I', stream, obj_at)]
            if placed['model'] not in models or floats(stream, at+16, 3) != placed['translation']:
                raise ValueError('placement/scenery mismatch')
            placed['scenery'].append(dict(offset=at, object_record=obj_at))
    shapes = shape_rows(data, key)
    if not definitions or not placements or not shapes or any(not p['scenery'] for p in placements):
        raise ValueError('named object provenance incomplete')
    return dict(evidence='static-original-file', name='ZCS_PylonA',
                definitions=definitions, placements=placements, shapes=shapes,
                actual_runtime_definition=None, actual_runtime_shape=None,
                event0_route_presence=None, kind4_identity=None)


def append_replay(records):
    """Synthetic replay of 5952E2..5952F8 / 472B8B..472BB6, not game execution."""
    sentinel = 'sentinel'
    links = {sentinel: [sentinel, sentinel]}
    for identity in records:
        if identity in links:
            raise ValueError('record identities must be unique')
        old_tail = links[sentinel][1]
        links[identity] = [None, None]
        links[old_tail][0] = identity
        links[sentinel][1] = identity
        links[identity][1] = old_tail
        links[identity][0] = sentinel
    result, current = [], links[sentinel][0]
    while current != sentinel:
        result.append(current)
        current = links[current][0]
    return result


def queue_replay(jobs, threshold):
    """Finite-input synthetic comparator/list replay for the pinned negative threshold."""
    if not math.isfinite(threshold) or threshold >= 0:
        raise ValueError('only the negative-threshold source branch is modeled')
    ordered = []
    for name, value in jobs:
        if not math.isfinite(value):
            raise ValueError('non-finite synthetic time')
        # 46ADE0 jumps to 46AE1A for abs(new-old) > negative threshold.
        # 46AE25/46AE2F returns 1 for new <= old, so insertion scans past it.
        index = 0
        while index < len(ordered) and value <= ordered[index][1]:
            index += 1
        ordered.insert(index, (name, value))
    return ordered


def output_path(workspace, root, requested):
    out = (workspace / requested).resolve()
    protected = Path('E:/Games').resolve()
    if out == root or root in out.parents or out in root.parents or out == protected or protected in out.parents:
        raise ValueError('output overlaps retail or protected E:/Games')
    if workspace not in out.parents:
        raise ValueError('output must be inside the current workspace')
    if any(ord(c) < 32 or ord(c) > 126 for c in out.relative_to(workspace).as_posix()):
        raise ValueError('output relative path must be printable ASCII')
    if out.exists():
        raise ValueError('output directory must be fresh')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    workspace = Path.cwd().resolve()
    root = (workspace / args.root).resolve()
    try:
        out = output_path(workspace, root, args.out)
        if not root.is_dir():
            raise ValueError('retail root must exist')
    except ValueError as error:
        parser.error(str(error))
    before = manifest(root)
    out.mkdir(parents=True, exist_ok=False)
    (out / '256-before.json').write_bytes(before)
    try:
        for relative, (size, sha) in INPUTS.items():
            if (root/relative).stat().st_size != size or digest_file(root/relative) != sha:
                raise ValueError('unsupported input identity: ' + relative)
        lines, facts = source_trace(Image((root/'speed.exe').read_bytes()))
        facts['inputs'] = INPUTS
        prop = named_prop(jdlz((root/'Tracks/TRACKB1003.lzc').read_bytes()),
                          (root/'Tracks/STREAML1RA.BUN').read_bytes())
        synthetic = dict(evidence='synthetic-python-replay-not-original-execution',
            definition_order=append_replay([r['offset'] for r in prop['definitions']]),
            shape_order=append_replay([r['header_record'] for r in prop['shapes']]),
            queue=queue_replay([('first', 0.002), ('later', 0.007), ('equal', 0.002)],
                               facts['constants']['00700088']['f32']))
        (out/'256-instructions.txt').write_text(lines, encoding='ascii')
        for name, value in [('source', facts), ('named-prop', prop), ('synthetic', synthetic)]:
            (out/('256-'+name+'.json')).write_text(
                json.dumps(value, ensure_ascii=True, allow_nan=False, indent=2)+'\n', encoding='ascii')
        print('PASS: pinned inputs, complete source ranges and checked call edges')
        print('PASS: distinct driver/physics stores, receiver and vtable slots')
        print('UNAVAILABLE: intervening driver-to-physics dispatch and placement')
        print('PASS: named prop definitions, duplicate hull records and scenery links')
        print('UNAVAILABLE: original execution, selector, cadence and contact matrix')
    finally:
        after = manifest(root)
        (out/'256-after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('PASS: retail root/directory/file/mtime/SHA-256 manifests identical')


if __name__ == '__main__':
    main()
