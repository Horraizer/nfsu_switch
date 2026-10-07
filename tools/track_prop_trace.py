"""Issue 270: bounded, read-only prop evidence from the pinned PC retail files.

Requires Python 3.8+ and Capstone. Run from a repository worktree; --out must
be fresh and outside the retail installation. Reports are research evidence,
not runtime contact measurements or an event0 route-membership decision.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


INPUTS = {
    'speed.exe': 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7',
    'Tracks/TRACKB1003.lzc': 'ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA',
    'Tracks/STREAML1RA.BUN': '2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E',
}

# Every range starts at a traced instruction boundary, ends immediately after
# an instruction, and excludes the unrelated code/padding between functions.
RANGES = [
    ('body-kind-store', 0x468340, 0x46835C),
    ('kind2-pair-remap', 0x469707, 0x46972B),
    ('definition-and-placement-loader', 0x5952A0, 0x5954B6),
    ('smokeable-factory', 0x595230, 0x595295),
    ('smokeable-state-accessors', 0x595140, 0x59515D),
    ('smokeable-inverse-mass', 0x595170, 0x595194),
    ('smokeable-inverse-inertia', 0x5951A0, 0x5951D2),
    ('pool-capacity', 0x595CDE, 0x595D65),
    ('prop-body-cleanup', 0x595530, 0x595667),
    ('virgin-constructor', 0x5962E0, 0x596486),
    ('virgin-destructor', 0x5964C0, 0x59650E),
    ('virgin-to-smokeable', 0x596570, 0x5966CF),
    ('smokeable-constructor-kind', 0x5966D0, 0x5967AF),
    ('smokeable-constructor-rigid-body', 0x5967AF, 0x5968A0),
    ('smokeable-constructor-shape-and-thresholds', 0x5968A0, 0x5969FC),
    ('rigid-body-constructor', 0x584800, 0x584920),
    ('rigid-body-inverse-properties', 0x584B10, 0x584B72),
    ('pose-initialization', 0x597140, 0x597381),
    ('smokeable-update', 0x597420, 0x597666),
    ('visible-pose-update', 0x597586, 0x5975C4),
    ('lifetime', 0x5977F0, 0x59791B),
    ('activation-and-break-budget', 0x597980, 0x597B7D),
    ('settling-gates', 0x597B80, 0x597C65),
    ('deactivate', 0x597C70, 0x597C8B),
    ('activate', 0x597C90, 0x597D0A),
    ('replace-with-children', 0x597D10, 0x597E03),
    ('child-availability', 0x597E10, 0x597E91),
    ('contact-child-replacement-gates', 0x420586, 0x420602),
    ('scrape-event-selector', 0x531080, 0x5310FB),
    ('impact-event-selector', 0x53139F, 0x5313CA),
    ('sentinel-helper', 0x40A880, 0x40A881),
    ('definition-placement-unload', 0x5954C0, 0x595525),
    ('prop-maintenance', 0x595670, 0x595748),
    ('pool-reclamation', 0x595750, 0x595B67),
    ('placement-exclusion', 0x595B70, 0x595BF0),
    ('prop-initialization', 0x595BF0, 0x595E95),
    ('prop-shutdown', 0x595EA0, 0x595F6B),
    ('prop-manager-serialization', 0x595F70, 0x5962B6),
    ('smokeable-effect-construction', 0x5969FC, 0x596C88),
    ('smokeable-pool-return', 0x596C90, 0x596CB9),
    ('smokeable-destruction', 0x596CC0, 0x596F4A),
    ('smokeable-serialization', 0x596F50, 0x597135),
    ('effect-motion', 0x597730, 0x5977E1),
    ('threshold-effect-enable', 0x597920, 0x597978),
    ('integration-state-classifier', 0x597EA0, 0x597F94),
    ('integration-state-writer', 0x597FA0, 0x5980CE),
    ('car-contact-clock-gate', 0x5980D0, 0x59812F),
    ('scenery-load', 0x4457C0, 0x445B57),
    ('scenery-unload', 0x445B60, 0x445C5F),
    ('world-construction', 0x41F6E0, 0x41F9A0),
    ('world-destruction', 0x41F9B0, 0x41FBFA),
    ('race-reconstruction', 0x421F00, 0x42212C),
    ('race-teardown', 0x422130, 0x422298),
    ('race-message-dispatch', 0x422BB0, 0x422C20),
    ('world-pre-contact-maintenance-slice', 0x420DE6, 0x420E2B),
    ('solver', 0x46BAE0, 0x46C61B),
    ('contact-record-construction', 0x465640, 0x4656F4),
    ('current-hit-clock-slice', 0x465830, 0x4658B5),
    ('contact-record-selection', 0x468820, 0x4688A0),
    ('audio-body-class', 0x52F6F0, 0x52F748),
    ('prop-clock-advance', 0x585E00, 0x585EBA),
    ('shape-forward-lookup', 0x472C60, 0x472CA9),
    ('start-set-track-byte-slice', 0x444C94, 0x444CCA),
    ('scenery-draw-mask-slice', 0x445EC0, 0x445EF1),
]

# Independently read instruction boundaries, not raw byte pattern xrefs.
# Each must be an instruction within a fully decoded range above.
INSTRUCTIONS = {
    0x5952E2: ('mov', 'eax, dword ptr [0x73777c]'),
    0x5952E7: ('mov', 'dword ptr [eax], esi'),
    0x5952E9: ('mov', 'dword ptr [0x73777c], esi'),
    0x5952F5: ('mov', 'dword ptr [esi + 4], eax'),
    0x5952F8: ('mov', 'dword ptr [esi], 0x737778'),
    0x59638C: ('mov', 'esi, dword ptr [0x737778]'),
    0x5963AD: ('mov', 'esi, dword ptr [esi]'),
    0x5954FB: ('mov', 'dword ptr [edx], ecx'),
    0x5954FD: ('mov', 'dword ptr [ecx + 4], edx'),
    0x5963BB: ('mov', 'dword ptr [edx + 0xc], edi'),
    0x5964F0: ('mov', 'dword ptr [eax + 0xc], 0'),
    0x5966B7: ('mov', 'dword ptr [esi + 0x170], eax'),
    0x445B0D: ('mov', 'eax, dword ptr [ecx]'),
    0x445B23: ('and', 'word ptr [eax + 0xe], bx'),
    0x445B36: ('or', 'word ptr [eax + 0xe], di'),
    0x445B7F: ('mov', 'dword ptr [eax*4 + 0x72a008], 0'),
    0x445EE9: ('test', 'dl, al'),
    0x41F93C: ('call', '0x595bf0'),
    0x41FA71: ('call', '0x595ea0'),
    0x595EA3: ('call', '0x595530'),
    0x4221FF: ('call', '0x41f9b0'),
    0x422BD4: ('call', '0x422130'),
    0x422BDA: ('call', '0x421f00'),
    0x421FB9: ('call', '0x41f6e0'),
    0x420E05: ('call', '0x595670'),
    0x5956CC: ('call', '0x5977f0'),
    0x5956F1: ('call', '0x5977f0'),
    0x59760F: ('call', '0x5977f0'),
    0x597ADC: ('mov', 'dword ptr [edi + 0x15c], 0'),
    0x5974D7: ('call', '0x597b80'),
    0x5974E5: ('call', '0x5980d0'),
    0x5974F4: ('mov', 'dword ptr [edi + 0x16c], edx'),
    0x597BC6: ('sub', 'esi, dword ptr [eax + 8]'),
    0x4656AF: ('mov', 'dword ptr [esi + 8], eax'),
    0x465894: ('mov', 'dword ptr [ebx + 0xc], ecx'),
    0x46BEED: ('call', 'dword ptr [edx + 0x48]'),
    0x46BF46: ('call', 'dword ptr [edx + 0x48]'),
    0x46C3EE: ('call', '0x465830'),
    0x46C5D2: ('call', 'dword ptr [edx + 0x4c]'),
    0x46C5E4: ('call', 'dword ptr [edx + 0x4c]'),
}


def digest_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest().upper()


def manifest(root):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('retail manifest does not support symlinks')
        info = path.stat()
        rows.append([path.relative_to(root).as_posix(),
                     'dir' if path.is_dir() else info.st_size, info.st_mtime_ns,
                     None if path.is_dir() else digest_file(path)])
    return (json.dumps(rows, ensure_ascii=True, separators=(',', ':')) + '\n').encode('ascii')


class Image:
    def __init__(self, data):
        self.data = data
        if len(data) < 64 or data[:2] != b'MZ':
            raise ValueError('invalid DOS header')
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if pe > len(data) - 24 or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE signature')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optsize = struct.unpack_from('<H', data, pe + 20)[0]
        if optsize < 32 or pe + 24 + optsize + 40 * count > len(data):
            raise ValueError('truncated PE sections')
        if struct.unpack_from('<H', data, pe + 24)[0] != 0x10B:
            raise ValueError('expected PE32')
        base = struct.unpack_from('<I', data, pe + 52)[0]
        self.sections = []
        for index in range(count):
            at = pe + 24 + optsize + 40 * index
            _, va, rawsize, raw = struct.unpack_from('<IIII', data, at + 8)
            if raw + rawsize > len(data):
                raise ValueError('raw section outside PE file')
            self.sections.append((base + va, rawsize, raw))

    def read(self, va, size):
        if size < 0:
            raise ValueError('negative PE read')
        for start, length, raw in self.sections:
            if start <= va and va + size <= start + length:
                return self.data[raw + va - start:raw + va - start + size]
        raise ValueError('VA outside raw PE sections: %08X' % va)

    def string(self, va):
        result = bytearray()
        for index in range(128):
            byte = self.read(va + index, 1)[0]
            if not byte:
                return result.decode('ascii')
            result.append(byte)
        raise ValueError('unterminated PE string')


def jdlz(blob):
    if len(blob) < 16 or blob[:8] != b'JDLZ\x02\x10\0\0':
        raise ValueError('unsupported JDLZ header')
    target, compressed = struct.unpack_from('<II', blob, 8)
    if compressed != len(blob) or target > 16 * 1024 * 1024:
        raise ValueError('invalid JDLZ sizes')
    decoded = bytearray()
    pos, flags1, flags2 = 16, 1, 1
    while len(decoded) < target:
        if flags1 == 1:
            if pos >= len(blob):
                raise ValueError('truncated JDLZ flag')
            flags1 = blob[pos] | 0x100
            pos += 1
        if flags2 == 1:
            if pos >= len(blob):
                raise ValueError('truncated JDLZ flag')
            flags2 = blob[pos] | 0x100
            pos += 1
        if flags1 & 1:
            if pos + 2 > len(blob):
                raise ValueError('truncated JDLZ reference')
            b0, b1 = blob[pos:pos + 2]
            pos += 2
            if flags2 & 1:
                length = (b1 | ((b0 & 0xF0) << 4)) + 3
                distance = (b0 & 0x0F) + 1
            else:
                distance = (b1 | ((b0 & 0xE0) << 3)) + 17
                length = (b0 & 0x1F) + 3
            flags2 >>= 1
            if distance > len(decoded) or length > target - len(decoded):
                raise ValueError('invalid JDLZ back-reference')
            for _ in range(length):
                decoded.append(decoded[-distance])
        else:
            if pos >= len(blob):
                raise ValueError('truncated JDLZ literal')
            decoded.append(blob[pos])
            pos += 1
        flags1 >>= 1
    return bytes(decoded), len(blob) - pos


def chunks(blob, begin=0, end=None, depth=0):
    if end is None:
        end = len(blob)
    if begin < 0 or end > len(blob) or begin > end:
        raise ValueError('invalid chunk bounds')
    if depth > 32:
        raise ValueError('chunk tree too deep')
    while begin < end:
        if end - begin < 8:
            raise ValueError('short chunk header')
        tag, size = struct.unpack_from('<II', blob, begin)
        finish = begin + 8 + size
        if finish > end:
            raise ValueError('chunk exceeds parent')
        yield tag, begin, finish
        if tag & 0x80000000:
            yield from chunks(blob, begin + 8, finish, depth + 1)
        begin = finish


def record_start(blob, offset, end, stride):
    start = (offset + 23) & ~15
    if start > end or (end - start) % stride:
        raise ValueError('invalid aligned record array')
    if any(byte != 0x11 for byte in blob[offset + 8:start]):
        raise ValueError('unexpected alignment filler')
    return start


def identity(blob, chunk, offset, size, file='Tracks/TRACKB1003.lzc'):
    if offset < 0 or size < 0 or offset + size > len(blob):
        raise ValueError('record outside input')
    return dict(file=file, chunk_offset=chunk, offset=offset, bytes=size,
                offset_space='decoded' if file.endswith('.lzc') else 'file',
                sha256=hashlib.sha256(blob[offset:offset + size]).hexdigest().upper())


def values(blob, offset, count):
    result = struct.unpack_from('<' + 'f' * count, blob, offset)
    if not all(math.isfinite(value) for value in result):
        raise ValueError('non-finite field in pinned input')
    return result


def track_rows(blob, categories):
    definitions, placements = [], []
    for tag, offset, end in chunks(blob):
        if tag == 0x34026:
            for at in range(record_start(blob, offset, end, 0x100), end, 0x100):
                name = blob[at + 8:at + 0x20].split(b'\0', 1)[0].decode('ascii')
                key, category = [struct.unpack_from('<I', blob, at + x)[0]
                                 for x in (0x80, 0xDC)]
                definitions.append(dict(offset=at, identity=identity(blob, offset, at, 256),
                    name=name, hash='%08X' % key,
                    descriptor_sha256=hashlib.sha256(blob[at+0x20:at+0x74]).hexdigest().upper(),
                    effect_hashes=['%08X' % struct.unpack_from('<I', blob, at+x)[0]
                                   for x in (0x84, 0x88)],
                    reclamation_exemption=struct.unpack_from('<I', blob, at+0xE4)[0],
                    category_hash='%08X' % category, category=categories.get(category, 'NONE'),
                    category_hash_matched=category in categories,
                    mass_field=values(blob, at + 0x20, 1)[0],
                    inertia_matrix=values(blob, at + 0x30, 16),
                    alternate_inertia_scale=values(blob, at + 0x70, 1)[0],
                    pivot=values(blob, at + 0xB0, 3),
                    lifetime_field=values(blob, at + 0xC0, 1)[0],
                    activation_field=values(blob, at + 0xC4, 1)[0],
                    break_budget_field=values(blob, at + 0xC8, 1)[0],
                    child_count=struct.unpack_from('<I', blob, at + 0x8C)[0],
                    child_hashes=['%08X' % struct.unpack_from('<I', blob, at + x)[0]
                                  for x in range(0xA0, 0xB0, 4)]))
        elif tag == 0x34027:
            for at in range(record_start(blob, offset, end, 0x70), end, 0x70):
                key, section, instance, model = struct.unpack_from('<IhhI', blob, at)
                placements.append(dict(offset=at, identity=identity(blob, offset, at, 112),
                    hash='%08X' % key, section=section,
                    instance=instance, model_hash='%08X' % model,
                    bounds_min=values(blob, at + 0x10, 3),
                    bounds_max=values(blob, at + 0x20, 3),
                    translation=values(blob, at + 0x60, 3)))
    if not definitions or not placements:
        raise ValueError('expected both prop arrays')
    for placement in placements:
        placement['definition_offsets'] = [row['offset'] for row in definitions
                                           if row['hash'] == placement['hash']]
        if not placement['definition_offsets']:
            raise ValueError('placement lacks a definition')
    return definitions, placements


def name_hash(name):
    result = 0xFFFFFFFF
    for byte in name.encode('ascii'):
        result = (33 * result + byte) & 0xFFFFFFFF
    return result


def scenery_rows(blob, placements):
    # Only top-level scenery containers are relevant; their children are
    # already documented in TRACK_OBJECTS.md. Keep every matching section.
    sections = {}
    for tag, offset, end in chunks(blob):
        if tag != 0x80034100:
            continue
        children = {tag: (at, finish) for tag, at, finish in chunks(blob, offset + 8, end)}
        head, head_end = children[0x34101]
        head = record_start(blob, head, head_end, 60)
        number = struct.unpack_from('<I', blob, head + 12)[0]
        at, finish = children[0x34103]
        start = record_start(blob, at, finish, 48)
        object_at, object_end = children[0x34102]
        if (object_end - object_at - 8) % 72:
            raise ValueError('invalid scenery object array')
        sections.setdefault(number, []).append((start, finish, object_at + 8, object_end, offset))
    linked = []
    for placement in placements:
        for start, finish, objects, object_end, container in sections.get(placement['section'], []):
            at = start + 48 * placement['instance']
            if placement['instance'] < 0 or at + 48 > finish:
                raise ValueError('placement instance outside scenery array')
            translation = values(blob, at + 16, 3)
            object_index = struct.unpack_from('<H', blob, at + 12)[0]
            object_at = objects + 72 * object_index
            if object_at + 72 > object_end:
                raise ValueError('instance object outside directory')
            mesh_hashes = ['%08X' % x for x in struct.unpack_from('<IIIIII', blob, object_at)]
            linked.append(dict(placement_offset=placement['offset'],
                section=placement['section'], instance=placement['instance'],
                scenery_offset=at, flags='%04X' % struct.unpack_from('<H', blob, at + 14)[0],
                scenery_identity=identity(blob, container, at, 48, 'Tracks/STREAML1RA.BUN'),
                object_identity=identity(blob, container, object_at, 72, 'Tracks/STREAML1RA.BUN'),
                mesh_hashes=mesh_hashes, model_in_mesh_slots=placement['model_hash'] in mesh_hashes,
                translation=translation,
                delta=[a - b for a, b in zip(translation, placement['translation'])]))
    return linked


def stream_bindings(track, placements, links):
    """Static file containment/start request eligibility; never live residency."""
    records = []
    for tag, chunk, end in chunks(track):
        if tag != 0x34107:
            continue
        if (end - chunk - 8) % 56:
            raise ValueError('partial stream directory record')
        for at in range(chunk + 8, end, 56):
            section = struct.unpack_from('<h', track, at + 8)[0]
            file_index, start, size = struct.unpack_from('<III', track, at + 16)
            records.append(dict(identity=identity(track, chunk, at, 56),
                section=section, track_byte=track[at+10], file_index=file_index,
                file_offset=start, read_bytes=size))
    result = []
    for placement in placements:
        candidates = [r for r in records if r['section'] == placement['section']]
        matches = [x for x in links if x['placement_offset'] == placement['offset']]
        if not candidates or not matches:
            raise ValueError('placement lacks stream directory or scenery')
        contained = []
        for record in candidates:
            for link in matches:
                at = link['scenery_offset']
                if (record['file_index'] == 1 and record['file_offset'] <= at and
                        at + 48 <= record['file_offset'] + record['read_bytes']):
                    contained.append(record)
        if not contained:
            raise ValueError('scenery not contained in its named stream range')
        result.append(dict(placement_offset=placement['offset'], stream_records=contained,
            live_route_presence='unavailable',
            static_track_byte_request=any(r['track_byte'] for r in contained)))
    return result


def hull_records(track, wanted):
    """Preserve every matching shape; no definition-to-hull zip or vertices."""
    result = []
    for tag, container, end in chunks(track):
        if tag != 0x80034020:
            continue
        children = list(chunks(track, container + 8, end))
        for index, (tag, at, finish) in enumerate(children):
            if tag != 0x39200:
                continue
            head = record_start(track, at, finish, 1)
            if finish - head < 0x58:
                raise ValueError('short hull header')
            key = '%08X' % struct.unpack_from('<I', track, head+0x18)[0]
            if key not in wanted:
                continue
            if index + 2 >= len(children):
                raise ValueError('missing hull arrays')
            ptag, pa, pe = children[index+1]
            itag, ia, ie = children[index+2]
            if (ptag, itag) != (0x39201, 0x39202):
                raise ValueError('unexpected hull arrays')
            ps, ix = record_start(track, pa, pe, 48), record_start(track, ia, ie, 2)
            result.append(dict(hash=key, header=identity(track, at, head, finish-head),
                points=identity(track, pa, ps, pe-ps), indices=identity(track, ia, ix, ie-ix),
                runtime_selection='unavailable', pivot_adjustment='unavailable'))
    return result


def list_replay(operations):
    """Synthetic replay of tail insertion/unlink and forward first match.

    Nodes have unique load-generation IDs, independent of exported hashes.
    This is not x86 emulation and no operation is a recovered live load.
    """
    sentinel = None
    nodes = {sentinel: [sentinel, sentinel, None]}
    queries = []
    for op in operations:
        if op[0] == 'load':
            _, node, key = op
            if node is None or node in nodes:
                raise ValueError('duplicate runtime node identity')
            tail = nodes[sentinel][1]
            nodes[tail][0] = node
            nodes[sentinel][1] = node
            nodes[node] = [sentinel, tail, key]
        elif op[0] == 'unload':
            node = op[1]
            if node is None or node not in nodes:
                raise ValueError('missing runtime node identity')
            nxt, prev, _ = nodes[node]
            nodes[prev][0] = nxt
            nodes[nxt][1] = prev
            del nodes[node]
        elif op[0] == 'find':
            node = nodes[sentinel][0]
            while node is not None and nodes[node][2] != op[1]:
                node = nodes[node][0]
            queries.append(node)
        else:
            raise ValueError('unknown list operation')
    order, node = [], nodes[sentinel][0]
    while node is not None:
        order.append(node)
        node = nodes[node][0]
    return dict(order=order, queries=queries)


def lifecycle_decision(placement, activated_at, flag_1a, construction_age,
                       activation_age, lifetime, reversion_delay):
    """Finite, normalized-time predicate replay of 0x5977F0, not a simulator."""
    if not all(math.isfinite(x) for x in
               (construction_age, activation_age, lifetime, reversion_delay)):
        raise ValueError('finite synthetic input required')
    recorded_activation = activated_at not in (0, 0x7FFFFFFF)
    if placement and not recorded_activation and not flag_1a and construction_age > reversion_delay:
        return 'attempt-proxy-then-delete-body'
    if recorded_activation and lifetime != 0 and activation_age >= lifetime:
        return 'callback-7c-then-delete-body'
    return 'retain'


def settle_decision(car_contact_age, has_contact, first_record_age, linear_speed,
                    angular_speed, constants):
    """Synthetic scalar comparisons; caller supplies already-converted ages."""
    if not all(math.isfinite(x) for x in
               (car_contact_age, first_record_age, linear_speed, angular_speed)):
        raise ValueError('finite synthetic input required')
    return (car_contact_age > constants['006B5A7C'] and has_contact and
            first_record_age > constants['006B5A78'] and
            linear_speed < constants['006B5A70'] and
            angular_speed < constants['006B5A74'])


def output_path(workspace, root, output):
    # Anchor before resolve: Windows Python 3.8 can leave nonexistent paths
    # relative. Resolve existing symlinks before containment checks.
    workspace = workspace.resolve()
    root, out = (workspace / root).resolve(), (workspace / output).resolve()
    games = Path('E:/Games').resolve()
    if out == root or root in out.parents or out in root.parents or out == games or games in out.parents:
        raise ValueError('output overlaps retail or protected E:/Games')
    if workspace not in out.parents:
        raise ValueError('output must be inside the current workspace')
    relative = out.relative_to(workspace)
    if any(ord(c) < 32 or ord(c) > 126 for c in relative.as_posix()):
        raise ValueError('output relative path must be ASCII')
    if not (relative.parts[0].startswith(('build', 'cmake-build-')) or relative.parts[0] == 'out'):
        raise ValueError('output must use an ignored build/out directory')
    if out.exists():
        raise ValueError('output directory must be fresh')
    return root, out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        root, out = output_path(Path.cwd(), args.root, args.out)
    except ValueError as error:
        parser.error(str(error))
    if not root.is_dir():
        parser.error('retail root must be a directory')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / '270-before.json').write_bytes(before)
    try:
        blobs, identities = {}, {}
        for relative, expected in INPUTS.items():
            blob = (root / relative).read_bytes()
            digest = hashlib.sha256(blob).hexdigest().upper()
            if digest != expected:
                raise ValueError('unsupported input SHA-256: ' + relative)
            blobs[relative] = blob
            identities[relative] = dict(size=len(blob), sha256=digest)
        image = Image(blobs['speed.exe'])
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        report = ['speed.exe SHA-256 ' + INPUTS['speed.exe']]
        decoded, ranges = {}, []
        for name, start, end in RANGES:
            blob = image.read(start, end - start)
            instructions = list(md.disasm(blob, start))
            if sum(insn.size for insn in instructions) != len(blob):
                raise ValueError('range does not decode completely: ' + name)
            decoded.update({i.address: (i.mnemonic, i.op_str) for i in instructions})
            ranges.append(dict(name=name, start='%08X' % start, end_exclusive='%08X' % end,
                               sha256=hashlib.sha256(blob).hexdigest().upper()))
            report.append('\n%s %08X..%08X sha256 %s' %
                          (name, start, end, hashlib.sha256(blob).hexdigest().upper()))
            report.extend('%08X %-8s %s' % (insn.address, insn.mnemonic, insn.op_str)
                          for insn in instructions)
        for address, expected in INSTRUCTIONS.items():
            if decoded.get(address) != expected:
                raise ValueError('instruction assertion failed at %08X: %r' %
                                 (address, decoded.get(address)))
        if image.read(0x40A880, 1) != b'\xc3':
            raise ValueError('sentinel helper must be RET')
        categories = {}
        for index in range(17):
            pointer = struct.unpack('<I', image.read(0x6EF240 + index * 4, 4))[0]
            name = image.string(pointer)
            categories[name_hash(name)] = name
        constant_addresses = (0x6B5A70, 0x6B5A74, 0x6B5A78, 0x6B5A7C, 0x6CCA38,
                              0x6CCC00, 0x6EF288, 0x6EF28C, 0x6EF290,
                              0x6B5A80, 0x6B5A84, 0x6B5A88, 0x6B5A8C, 0x6B5A90,
                              0x6CC844, 0x6CC93C, 0x6CCB4C, 0x6CCB74, 0x6CCB8C,
                              0x6CCD60, 0x6CC9FC, 0x6CC7DC)
        constants = {('%08X' % va): struct.unpack('<f', image.read(va, 4))[0]
                     for va in constant_addresses}
        track, unused = jdlz(blobs['Tracks/TRACKB1003.lzc'])
        definitions, placements = track_rows(track, categories)
        linked = scenery_rows(blobs['Tracks/STREAML1RA.BUN'], placements)
        for placement in placements:
            matches = [x for x in linked if x['placement_offset'] == placement['offset']]
            if not matches or any(not x['model_in_mesh_slots'] or any(x['delta']) for x in matches):
                raise ValueError('inconsistent placement/scenery identity')
        bindings = stream_bindings(track, placements, linked)
        hulls = hull_records(track, {p['hash'] for p in placements})
        # Decode the dispatch table as data, not x86 instructions. The only
        # asserted meaning is the body of the cleanup/recreate case.
        case_targets = struct.unpack('<IIIII', image.read(0x422C20, 20))
        selectors = image.read(0x422C34, 10)
        if any(index >= len(case_targets) for index in selectors):
            raise ValueError('invalid message dispatch selector')
        messages = {str(index+1): '%08X' % case_targets[selector]
                    for index, selector in enumerate(selectors)}
        duplicates = [r for r in definitions if r['hash'] == '5087DB01']
        operations = [('load', str(r['offset']), r['hash']) for r in duplicates]
        operations += [('find', '5087DB01')]
        local_order = list_replay(operations)
        synthetic = dict(evidence='synthetic-python-predicates-not-native-or-original-runtime',
            assumptions='initially empty list, only named duplicate records in array order',
            list_order=local_order,
            reversion=lifecycle_decision(True, 0, False, 3, 0, 0, constants['006CCC00']),
            activated_no_lifetime=lifecycle_decision(True, 4000, False, 30, 30, 0, constants['006CCC00']))
        summary = dict(inputs=identities, track_decoded_size=len(track), unused_input=unused,
                       definitions=definitions, placements=placements, scenery_links=linked,
                       evidence='static-original-files', ranges=ranges,
                       checked_instructions=len(INSTRUCTIONS),
                       stream_bindings=bindings, hull_records=hulls,
                       race_message_targets=messages, synthetic=synthetic,
                       integration_interface={('%02X' % offset): '%08X' % struct.unpack(
                           '<I', image.read(0x6B8B08+offset, 4))[0] for offset in (0, 4, 8)},
                       runtime=dict(event='introductory event0 / track1003 forward',
                           body_descriptor_winner='unavailable', hull_winner='unavailable',
                           visible_route_contact='unavailable', restart_ui_message='unavailable',
                           clock_cadence='unavailable', audio_bank_sample='unavailable'),
                       constants=constants,
                       constant_bits={('%08X' % va): '%08X' % struct.unpack('<I', image.read(va, 4))[0]
                                      for va in constant_addresses},
                       categories={('%08X' % k): v for k, v in categories.items()},
                       vtables={('%08X' % va): ['%08X' % x for x in
                           struct.unpack('<' + 'I' * count, image.read(va, 4 * count))]
                           for va, count in ((0x6B8A90, 30), (0x6B8B18, 32))})
        (out / '270-instructions.txt').write_text('\n'.join(report) + '\n', encoding='ascii')
        (out / '270-props.json').write_text(json.dumps(summary, ensure_ascii=True, indent=2,
                                                    allow_nan=False) + '\n', encoding='ascii')
    finally:
        after = manifest(root)
        (out / '270-after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('PASS: retail manifests identical (paths, directories, size, mtime, SHA-256)')
    print('PASS: pinned PE lifecycle/reset/streaming ranges and %d instruction assertions' % len(INSTRUCTIONS))
    print('PASS: definitions=%d placements=%d scenery_links=%d stream_bindings=%d hull_records=%d' %
          (len(definitions), len(placements), len(linked), len(bindings), len(hulls)))
    print('UNAVAILABLE: original event0 runtime, live winners, reset UI trace, audio mapping and hardware')


if __name__ == '__main__':
    main()
