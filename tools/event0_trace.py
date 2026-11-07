"""Read-only issue 268 retail setup/HUD research; Python 3.8+ and Capstone.

Reports go to a fresh directory outside all input roots. No game is launched.
--range and --refs are exploratory evidence, not automatic semantic labels.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


INPUTS = {
    'speed.exe': 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7',
    'FrontEnd/FrontB.lzc': '76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90',
    'Global/GLOBALB.BUN': '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9',
    'Tracks/TRACKB1003.lzc': 'ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA',
}

# End addresses are exclusive. Entire consumer functions, including their
# tail branches, are retained; grouped spans also include padding/data.
RANGES = [
    ('new_career', 0x4B37D0, 0x4B388B),
    ('career_reset', 0x5A0A00, 0x5A0B33),
    ('event_select', 0x5A0CE0, 0x5A0DB2),
    ('event_copy', 0x5A2350, 0x5A2537),
    ('setup_init', 0x5A2930, 0x5A29A6),
    ('career_setup', 0x4B5DF0, 0x4B62DA),
    ('preset_apply', 0x4B26F0, 0x4B2A9C),
    ('rival_setup', 0x4B4040, 0x4B4371),
    ('traffic_setup', 0x4B3AC0, 0x4B3C5D),
    ('actor_config', 0x5809C0, 0x580A39),
    ('adaptive_tuning', 0x57F700, 0x57F96E),
    ('ride_init', 0x417600, 0x417691),
    ('grid_swap', 0x4B6300, 0x4B638D),
    ('grid_load', 0x422580, 0x422A69),
    ('grid_instantiate', 0x422A70, 0x422B70),
    ('actor_instantiate', 0x422410, 0x42257D),
    ('live_rank', 0x434ED0, 0x434F7F),
    ('progress_to_time', 0x433340, 0x4333B3),
    ('gap', 0x4353E0, 0x435568),
    ('status_update', 0x435570, 0x435A64),
    ('finish_place', 0x435BD0, 0x435C08),
    ('countdown', 0x434D10, 0x434DC5),
    ('countdown_phase', 0x434DD0, 0x434ED0),
    ('hud_construct', 0x4A8E40, 0x4A9264),
    ('hud_update', 0x4A9440, 0x4A9533),
    ('hud_visibility', 0x4A9540, 0x4A980C),
    ('hud_mask', 0x42E620, 0x42EB97),
    ('race_settings', 0x431DF0, 0x431F3D),
    ('preset_vinyl_slot', 0x4B26B0, 0x4B26C3),
    ('preset_decal_slot', 0x4B26D0, 0x4B26E3),
    ('gauge_update', 0x4A0F50, 0x4A1287),
    ('gauge_map', 0x4A0EA0, 0x4A0F4F),
    ('turbo_update', 0x4A13A0, 0x4A145B),
    ('nitrous_update', 0x4A64B0, 0x4A6792),
    ('lap_construct', 0x4A1580, 0x4A181F),
    ('lap_update', 0x4A1820, 0x4A1B44),
    ('split_construct', 0x4A1B50, 0x4A1BED),
    ('split_update', 0x4A1BF0, 0x4A1CE9),
    ('position_construct', 0x4A00C0, 0x4A019B),
    ('position_update', 0x4A01A0, 0x4A02A4),
    ('leaderboard_update', 0x4A2B10, 0x4A2EF0),
    ('leaderboard_snapshot', 0x4A3930, 0x4A3A99),
    ('wrong_way_update', 0x4A3C60, 0x4A3EFF),
    ('finish_construct', 0x4A7C90, 0x4A7D5E),
    ('finish_update', 0x4A7D60, 0x4A7F8E),
    ('countdown_construct', 0x4A7F90, 0x4A7FFA),
    ('countdown_update', 0x4A8000, 0x4A8134),
    ('map_construct', 0x4A8140, 0x4A81FC),
    ('style_construct', 0x4A8200, 0x4A837D),
    ('style_update', 0x4A83C0, 0x4A85D6),
]
# Search/display context, explicitly not a claim of function boundaries.
CONTEXT_RANGES = [
    ('mode_package_context', 0x42B300, 0x42BAB3),
    ('mask_context', 0x42E600, 0x42EDB0),
    ('nitrous_context', 0x4A5CA0, 0x4A635C),
    ('split_helpers_context', 0x4A1D00, 0x4A1F31),
    ('stage_context', 0x434B50, 0x434D10),
    ('clock_context', 0x585E00, 0x585ED0),
    ('finish_state_context', 0x4350D0, 0x4353E0),
    ('status_update_context', 0x435570, 0x435A70),
    ('race_settings_context', 0x431DF0, 0x432020),
    ('map_actor_context', 0x58EF30, 0x58F990),
    ('map_init_context', 0x58E540, 0x58E900),
    ('movie_finish_context', 0x4E84C0, 0x4E863E),
]


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
            raise ValueError('input manifest does not support symlinks')
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
        count, optsize = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
        if optsize < 32 or pe + 24 + optsize + 40 * count > len(data):
            raise ValueError('truncated PE sections')
        if struct.unpack_from('<H', data, pe + 24)[0] != 0x10B:
            raise ValueError('expected PE32')
        base = struct.unpack_from('<I', data, pe + 52)[0]
        self.sections = []
        for index in range(count):
            at = pe + 24 + optsize + 40 * index
            name = data[at:at + 8].rstrip(b'\0').decode('ascii')
            _, va, rawsize, raw = struct.unpack_from('<IIII', data, at + 8)
            if raw > len(data) or rawsize > len(data) - raw:
                raise ValueError('raw section outside PE file')
            self.sections.append((base + va, rawsize, raw, name))

    def read(self, va, size):
        if size < 0:
            raise ValueError('negative read')
        for start, length, raw, _ in self.sections:
            if start <= va and size <= length - (va - start):
                return self.data[raw + va - start:raw + va - start + size]
        raise ValueError('VA outside raw PE sections: %08X' % va)

    def string(self, va):
        result = bytearray()
        for index in range(256):
            byte = self.read(va + index, 1)[0]
            if not byte:
                return result.decode('ascii')
            result.append(byte)
        raise ValueError('unterminated PE string')


def jdlz(blob):
    if len(blob) < 16 or blob[:8] != b'JDLZ\x02\x10\0\0':
        raise ValueError('unsupported JDLZ header')
    target, compressed = struct.unpack_from('<II', blob, 8)
    if compressed != len(blob) or target > 32 * 1024 * 1024:
        raise ValueError('invalid JDLZ sizes')
    decoded = bytearray()
    pos, flags1, flags2 = 16, 1, 1

    def take(count):
        nonlocal pos
        if count > len(blob) - pos:
            raise ValueError('truncated JDLZ token')
        value = blob[pos:pos + count]
        pos += count
        return value

    while len(decoded) < target:
        if flags1 == 1:
            flags1 = take(1)[0] | 0x100
        if flags2 == 1:
            flags2 = take(1)[0] | 0x100
        if flags1 & 1:
            b0, b1 = take(2)
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
            decoded.append(take(1)[0])
        flags1 >>= 1
    return bytes(decoded)


def chunks(blob, begin=0, end=None, depth=0):
    if end is None:
        end = len(blob)
    if depth > 32 or begin < 0 or end > len(blob) or begin > end:
        raise ValueError('invalid chunk bounds/depth')
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


def name_hash(value, fold=True):
    result = 0xFFFFFFFF
    for byte in (value.upper() if fold else value).encode('ascii'):
        result = (result * 33 + byte) & 0xFFFFFFFF
    return result


def table_span(blob, wanted, stride=None, align=1):
    found = [(at, end) for tag, at, end in chunks(blob) if tag == wanted]
    if len(found) != 1:
        raise ValueError('expected one table: %08X' % wanted)
    at, end = found[0]
    first = (at + 8 + align - 1) // align * align
    if first > end or (stride and (end - first) % stride):
        raise ValueError('partial/alignment-invalid table: %08X' % wanted)
    return first, end


def ascii_string(blob, at, length, allow_empty=False):
    value = blob[at:at + length]
    if len(value) != length or b'\0' not in value:
        raise ValueError('unterminated/short string')
    value = value.split(b'\0')[0]
    if (not value and not allow_empty) or any(not 32 <= byte <= 126 for byte in value):
        raise ValueError('empty/non-printable string')
    return value.decode('ascii')


def event_records(blob, presets):
    first, end = table_span(blob, 0x34A02, 332)
    numbers, selected = set(), []
    for record in range(first, end, 332):
        number, group, mode = struct.unpack_from('<3I', blob, record)
        if number >= 256 or number in numbers:
            raise ValueError('invalid/duplicate event number')
        numbers.add(number)
        if number not in (0, 1):
            continue
        count = struct.unpack_from('<I', blob, record + 0x98)[0]
        rivals_count = struct.unpack_from('<I', blob, record + 0x128)[0]
        if not 1 <= count <= 8 or rivals_count > 3:
            raise ValueError('event count outside consumer bounds')
        races = []
        for index in range(count):
            track, laps, direction = struct.unpack_from('<hbb', blob, record + 0x78 + 4 * index)
            if track <= 0 or laps <= 0 or direction not in (0, 1):
                raise ValueError('invalid race slot')
            races.append(dict(track=track, laps=laps, direction=direction))
        rivals = []
        keys = [struct.unpack_from('<I', blob, record + 0xD0)[0]]
        for k in range(rivals_count):
            rival = record + 0xD4 + k * 0x1C
            key = struct.unpack_from('<I', blob, rival + 0xC)[0]
            keys.append(key)
            rivals.append(dict(offset=rival, name=ascii_string(blob, rival, 8),
                               preset_hash=hex(key), difficulty_values=struct.unpack_from('<3h', blob, rival + 0x10),
                               tuning_selector=struct.unpack_from('<h', blob, rival + 0x16)[0],
                               ai_byte=blob[rival + 0x18]))
        # Zero selects the profile/random fallback; a missing nonzero hash
        # must never silently turn a fixed roster into a partial report.
        if any(key and key not in presets for key in keys):
            raise ValueError('unresolved nonzero event preset')
        if any(key and (not presets[key]['car'] or not presets[key]['name']) for key in keys):
            raise ValueError('empty selected preset identity')
        selected.append(dict(number=number, offset=record, group=group, mode=mode,
                             races=races, rivals=rivals, player_preset_hash=hex(keys[0]),
                             words=[hex(value) for value in struct.unpack_from('<83I', blob, record)],
                             presets=[presets[key] if key else None for key in keys]))
    if {event['number'] for event in selected} != {0, 1}:
        raise ValueError('missing event0/event1 comparison')
    return selected


def grid_records(blob):
    first, end = table_span(blob, 0x34146, 48, 16)
    result, slots = [], set()
    for pos in range(first, end, 48):
        key, slot = struct.unpack_from('<II', blob, pos + 8)
        if key != name_hash('ForwardStartingPosition', fold=False):
            continue
        xyz = struct.unpack_from('<3f', blob, pos + 0x10)
        if slot in slots or slot >= 8 or not all(math.isfinite(value) for value in xyz):
            raise ValueError('invalid/duplicate forward grid marker')
        slots.add(slot)
        result.append(dict(offset=pos, marker_index=slot, raw_position=xyz,
                           angle_u16=struct.unpack_from('<H', blob, pos + 0x20)[0]))
    if not {0, 1, 2, 3}.issubset(slots):
        raise ValueError('missing four-racer grid markers')
    return sorted(result, key=lambda row: row['marker_index'])


def track_record(blob):
    first, end = table_span(blob, 0x34201, 252)
    matches = [pos for pos in range(first, end, 252)
               if struct.unpack_from('<h', blob, pos + 0x6C)[0] == 1003]
    if len(matches) != 1:
        raise ValueError('expected unique track1003')
    pos = matches[0]
    traffic = struct.unpack_from('<8b', blob, pos + 0xC0)
    if traffic[2] < 0:
        raise ValueError('negative event0 traffic count')
    return dict(offset=pos, traffic_counts=traffic, event0_traffic_configs=traffic[2],
                lap_skip=blob[pos + 0x69])


def part_catalog(blob, presets):
    # Catalog evidence only: the retail consumer implements fallback/disable
    # rules. This does not claim every hash is an installed mesh.
    first, end = table_span(blob, 0x34604, 48)
    text, text_end = table_span(blob, 0x34606)
    wanted = {value for preset in presets if preset
              for slot, value in preset['pairs'] if slot < 120 and value}
    result = {}
    for pos in range(first, end, 48):
        offset, car, key = struct.unpack_from('<3I', blob, pos)
        if key not in wanted:
            continue
        if offset >= text_end - text:
            raise ValueError('part name offset outside table')
        raw = blob[text + offset:text_end].split(b'\0', 1)
        if len(raw) != 2 or any(byte not in (9, 10, 13) and not 32 <= byte <= 126 for byte in raw[0]):
            raise ValueError('invalid part catalog name')
        row = dict(offset=pos, name=raw[0].decode('ascii'), car_hash=hex(car),
                   slot=struct.unpack_from('<b', blob, pos + 0x10)[0],
                   upgrade=blob[pos + 0x11],
                   models=[hex(value) for value in struct.unpack_from('<4I', blob, pos + 0x20)])
        result.setdefault(hex(key), []).append(row)
    return result


def prepare_output(roots, out):
    roots, out = [root.resolve() for root in roots], out.resolve()
    if any(not root.is_dir() for root in roots):
        raise ValueError('input root does not exist')
    if any(out == root or root in out.parents or out in root.parents for root in roots):
        raise ValueError('output overlaps an input root')
    # Existing workspace ancestors may be non-ASCII; only newly created names
    # are constrained. Reject before any directory is written.
    for path in [out] + list(out.parents):
        if path.exists():
            break
        if any(not 32 <= ord(ch) <= 126 for ch in path.name):
            raise ValueError('new output name must be printable ASCII')
    out.mkdir(parents=True, exist_ok=False)
    return roots, out


def preset_records(blob):
    found = [(at, end) for tag, at, end in chunks(blob) if tag == 0x30220]
    if len(found) != 1:
        raise ValueError('expected one preset table')
    at, end = found[0]
    if (end - at - 8) % 0x408:
        raise ValueError('partial preset record')
    result = {}
    for pos in range(at + 8, end, 0x408):
        car = ascii_string(blob, pos + 8, 32, allow_empty=True)
        name = ascii_string(blob, pos + 0x28, 32, allow_empty=True)
        key = name_hash(name)
        if key in result:
            raise ValueError('duplicate preset hash')
        pairs = [struct.unpack_from('<II', blob, pos + 0x48 + 8 * k) for k in range(120)]
        if any(slot > 120 and slot != 0xFFFFFFFF for slot, _ in pairs):
            raise ValueError('preset slot outside consumer bounds')
        result[key] = dict(offset=pos, car=car, name=name, hash=hex(key), pairs=pairs)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--range', action='append', default=[], help='exploratory START:END hex VAs')
    parser.add_argument('--refs', action='append', default=[], help='exploratory hex address')
    parser.add_argument('--string', action='append', default=[], help='exploratory hex string VA')
    parser.add_argument('--find', action='append', default=[], help='ASCII string search, evidence only')
    parser.add_argument('--words', action='append', default=[], help='exploratory hex VA:COUNT u32 table')
    args = parser.parse_args()
    roots = [args.root.resolve()]
    if args.reference:
        roots.append(args.reference.resolve())
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    roots, out = prepare_output(roots, args.out)
    before = []
    try:
        for index, root in enumerate(roots):
            before.append(manifest(root))
            (out / ('before-%d.json' % index)).write_bytes(before[-1])
        blobs = {}
        provenance = {}
        for name, expected in INPUTS.items():
            blob = (roots[0] / name).read_bytes()
            digest = hashlib.sha256(blob).hexdigest().upper()
            if digest != expected:
                raise ValueError('unsupported input SHA-256: ' + name)
            blobs[name] = blob
            provenance[name] = dict(size=len(blob), sha256=digest)
        image = Image(blobs['speed.exe'])
        slot_names = {}
        for index in range(120):
            slot, pointer = struct.unpack('<II', image.read(0x6F0970 + 8 * index, 8))
            if slot != index:
                raise ValueError('invalid retail slot-name table')
            slot_names[str(slot)] = image.string(pointer)
        for value in args.find:
            if not value:
                raise ValueError('empty search token')
            token = value.encode('ascii')
            for start, length, raw, _ in image.sections:
                data = image.data[raw:raw + length]
                pos = data.find(token)
                while pos >= 0:
                    print('FIND speed.exe %s VA %08X' % (value, start + pos))
                    pos = data.find(token, pos + 1)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        md.detail = True
        from capstone.x86 import X86_OP_IMM, X86_OP_MEM
        strings, constants = {}, {}
        ranges = list(RANGES) + list(CONTEXT_RANGES)
        for value in args.range:
            start, end = (int(part, 16) for part in value.split(':'))
            if not start < end or end - start > 0x10000:
                raise ValueError('invalid range')
            ranges.append(('explore', start, end))
        report = []
        for name, start, end in ranges:
            blob = image.read(start, end - start)
            context = name == 'explore' or name.endswith('_context')
            md.skipdata = context
            instructions = list(md.disasm(blob, start))
            for item in instructions:
                if not item.id:
                    continue
                for op in item.operands:
                    address = op.imm if op.type == X86_OP_IMM else op.mem.disp if op.type == X86_OP_MEM else 0
                    if any(va <= address < va + length and section == '.rdata'
                           for va, length, _, section in image.sections):
                        if op.type == X86_OP_IMM:
                            try:
                                value = image.string(address)
                                if len(value) >= 3 and all(32 <= ord(ch) <= 126 for ch in value):
                                    strings[hex(address)] = value
                            except (ValueError, UnicodeDecodeError):
                                pass
                        elif op.type == X86_OP_MEM:
                            value = image.read(address, 4)
                            f32 = struct.unpack('<f', value)[0]
                            constants[hex(address)] = dict(u32=hex(struct.unpack('<I', value)[0]),
                                                          f32=f32 if math.isfinite(f32) else None)
            report.append('\n%s %08X..%08X SHA256 %s' % (name, start, end, hashlib.sha256(blob).hexdigest().upper()))
            report.extend('%08X %-8s %s' % (item.address, item.mnemonic, item.op_str) for item in instructions)
            if sum(item.size for item in instructions) != len(blob):
                if not context:
                    raise ValueError('range does not fully decode: ' + name)
                report.append('CONTEXT PARTIAL END: %08X' % (start + sum(item.size for item in instructions)))
        (out / 'disassembly.txt').write_text('\n'.join(report) + '\n', encoding='ascii')
        (out / 'operands.json').write_text(json.dumps(dict(strings=strings, constants=constants), indent=2, ensure_ascii=True) + '\n', encoding='ascii')
        for value in args.string:
            print('STRING %08X %s' % (int(value, 16), image.string(int(value, 16))))
        for value in args.words:
            va, count = (int(part, 16) for part in value.split(':'))
            if not 1 <= count <= 128:
                raise ValueError('invalid words count')
            print('WORDS %08X %s' % (va, [hex(word) for word in struct.unpack('<' + 'I' * count, image.read(va, 4 * count))]))
        if args.refs:
            wanted = {int(value, 16) for value in args.refs}
            md.detail = True
            from capstone.x86 import X86_OP_IMM, X86_OP_MEM
            refs = []
            for start, length, raw, name in image.sections:
                if name != '.text':
                    continue
                # Linear section decode is a search index only. Validate each
                # hit in an explicit boundary-checked function range.
                md.skipdata = True
                for item in md.disasm(image.data[raw:raw + length], start):
                    if item.id and any((op.type == X86_OP_IMM and op.imm in wanted) or
                                       (op.type == X86_OP_MEM and op.mem.disp in wanted)
                                       for op in item.operands):
                        refs.append('%08X %-8s %s' % (item.address, item.mnemonic, item.op_str))
            (out / 'refs.txt').write_text('\n'.join(refs) + '\n', encoding='ascii')
        tables, decoded = {}, {}
        for name in ('FrontEnd/FrontB.lzc', 'Global/GLOBALB.BUN', 'Tracks/TRACKB1003.lzc'):
            blob = blobs[name]
            if blob[:4] == b'JDLZ':
                blob = jdlz(blob)
            decoded[name] = blob
            for value in args.find:
                token = value.encode('ascii')
                hits = []
                pos = blob.find(token)
                while pos >= 0:
                    hits.append(pos)
                    pos = blob.find(token, pos + 1)
                print('FIND %s %s %s' % (name, value, hits))
            tables[name] = dict(size=len(blob), sha256=hashlib.sha256(blob).hexdigest().upper(),
                               chunks=[dict(tag=hex(tag), offset=at, size=end - at - 8)
                                       for tag, at, end in chunks(blob)])
            if name == 'FrontEnd/FrontB.lzc':
                presets = preset_records(blob)
                events = event_records(blob, presets)
                tables[name]['events'] = events
        tables['Global/GLOBALB.BUN']['track1003'] = track_record(decoded['Global/GLOBALB.BUN'])
        tables['Global/GLOBALB.BUN']['preset_hash_catalog'] = part_catalog(
            decoded['Global/GLOBALB.BUN'], [preset for event in events for preset in event['presets']])
        tables['Tracks/TRACKB1003.lzc']['forward_grid'] = grid_records(decoded['Tracks/TRACKB1003.lzc'])
        (out / 'data.json').write_text(json.dumps(dict(provenance=provenance, tables=tables,
                                                     slot_names=slot_names), indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    finally:
        for index, root in enumerate(roots[:len(before)]):
            after = manifest(root)
            (out / ('after-%d.json' % index)).write_bytes(after)
            if after != before[index]:
                raise ValueError('input manifest changed: %d' % index)
            print('PASS: input %d manifest unchanged SHA256 %s' % (index, hashlib.sha256(after).hexdigest().upper()))
    print('PASS: fresh evidence ' + out.name)


if __name__ == '__main__':
    main()
