"""Read-only lap/time HUD research. Python 3.8+ and Capstone 5 required.

Fresh reports only; input trees are hashed before and after every trace.
No executable is launched and no extracted retail assets enter Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXE_SHA = 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'
BUNDLE_SHA = '044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299'
LANGUAGE_SHA = '6CBAB64235523149B95966B1844CDCA9FB858744126F8D9AD10E72C921AB8790'
RANGES = (
    ('lap_constructor', 0x4A1580, 0x4A181F),
    ('lap_update', 0x4A1820, 0x4A1B44),
    ('component_visibility', 0x4A9540, 0x4A980C),
    ('visibility_mask', 0x42E620, 0x42EB97),
    ('format_ticks', 0x586580, 0x586639),
    ('split_time_fields', 0x5864F0, 0x586576),
    ('format_fields', 0x586090, 0x586485),
    ('text_draw', 0x4F48B0, 0x4F4CF2),
    ('glyph_geometry', 0x4F4790, 0x4F48A7),
    ('bind_object_resource', 0x5003C0, 0x500410),
    ('text_submit', 0x4F25F0, 0x4F27CC),
    ('text_fields', 0x501AC0, 0x501B77),
)
CONTEXT = (
    ('object_resource_index', 0x501887, 0x5018AC),
    ('resource_request_copy', 0x501248, 0x5012E7),
)


def sha(data):
    return hashlib.sha256(data).hexdigest().upper()


def manifest(root):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('symlink in input tree')
        info = path.stat()
        digest = None
        if path.is_file():
            value = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    value.update(block)
            digest = value.hexdigest().upper()
        elif not path.is_dir():
            raise ValueError('unsupported input entry')
        rows.append([path.relative_to(root).as_posix(),
                     'dir' if path.is_dir() else info.st_size,
                     info.st_mtime_ns, digest])
    return (json.dumps(rows, ensure_ascii=True, separators=(',', ':')) + '\n').encode('ascii')


class PE:
    def __init__(self, data):
        self.data, self.sections = data, []
        if len(data) < 64 or data[:2] != b'MZ':
            raise ValueError('invalid DOS header')
        pe = struct.unpack_from('<I', data, 60)[0]
        if pe > len(data) - 24 or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE header')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        opt = struct.unpack_from('<H', data, pe + 20)[0]
        if opt < 32 or pe + 24 + opt + 40 * count > len(data):
            raise ValueError('truncated PE section table')
        if struct.unpack_from('<H', data, pe + 24)[0] != 0x10B:
            raise ValueError('expected PE32')
        base = struct.unpack_from('<I', data, pe + 52)[0]
        for index in range(count):
            at = pe + 24 + opt + 40 * index
            _, va, size, raw = struct.unpack_from('<IIII', data, at + 8)
            if raw > len(data) or size > len(data) - raw:
                raise ValueError('truncated PE section')
            self.sections.append((base + va, size, raw))

    def read(self, va, size):
        if size < 0:
            raise ValueError('negative PE read')
        for start, length, raw in self.sections:
            if start <= va and size <= length - (va - start):
                return self.data[raw + va - start:raw + va - start + size]
        raise ValueError('unmapped PE read: %08X' % va)


def jdlz(data, limit=16 * 1024 * 1024):
    if len(data) < 16 or data[:8] != b'JDLZ\x02\x10\0\0':
        raise ValueError('invalid JDLZ header')
    target, size = struct.unpack_from('<II', data, 8)
    if target > limit or size < 16 or size > len(data):
        raise ValueError('invalid JDLZ bounds')
    at, first, second, out = 16, 1, 1, bytearray()

    def byte():
        nonlocal at
        if at >= size:
            raise ValueError('truncated JDLZ stream')
        value = data[at]
        at += 1
        return value

    while len(out) < target:
        if first == 1:
            first = byte() | 256
        if second == 1:
            second = byte() | 256
        if first & 1:
            a, b = byte(), byte()
            if second & 1:
                length, distance = (b | ((a & 240) << 4)) + 3, (a & 15) + 1
            else:
                length, distance = (a & 31) + 3, (b | ((a & 224) << 3)) + 17
            second >>= 1
            if distance > len(out) or length > target - len(out):
                raise ValueError('invalid JDLZ back-reference')
            for _ in range(length):
                out.append(out[-distance])
        else:
            out.append(byte())
        first >>= 1
    return bytes(out)


def chunks(data):
    at = 0
    while at < len(data):
        if at + 8 > len(data):
            raise ValueError('truncated chunk header')
        kind, size = struct.unpack_from('<II', data, at)
        begin, end = at + 8, at + 8 + size
        if end > len(data):
            raise ValueError('truncated chunk payload')
        yield kind, begin, end
        at = end


def fonts(data):
    rows = []
    for kind, begin, end in chunks(data):
        if kind != 0x30201:
            continue
        if end - begin < 640:
            raise ValueError('truncated font header')
        name = data[begin:begin + 256].split(b'\0', 1)[0].decode('ascii')
        at = begin + 512
        if data[at:at + 4] != b'FNTF':
            raise ValueError('invalid font signature')
        count = struct.unpack_from('<H', data, at + 10)[0]
        flags = struct.unpack_from('<I', data, at + 12)[0]
        if not flags & 0x40000 or at + 128 + count * 16 > end:
            raise ValueError('unsupported font glyph layout')
        glyphs = []
        for index in range(count):
            offset = at + 128 + index * 16
            code, width, height, x, y = struct.unpack_from('<HBBHH', data, offset)
            if code in set(b'0123456789:./-LAPCURRENTBEST '):
                bx, by = struct.unpack_from('<bb', data, offset + 9)
                advance = struct.unpack_from('<h', data, offset + 14)[0]
                glyphs.append(dict(offset=offset, code=code, x=x, y=y,
                                   width=width, height=height,
                                   bearing_x=bx, bearing_y=by, advance=advance))
        texture = data[begin+256:begin+512].split(b'\0', 1)[0].decode('ascii')
        rows.append(dict(name=name, texture_name=texture, fntf_offset=at, header=data[at:at + 128].hex(),
                         glyphs=glyphs))
    return rows


def feng_objects(data, name_parts=('lap', 'time')):
    """Decode bounded ObjD fields; field tags are two bytes plus u16 size."""
    result = []
    for match in re.finditer(rb'FOb\xea', data):
        start = match.start()
        if start + 16 > len(data):
            raise ValueError('truncated FEng object header')
        length = struct.unpack_from('<I', data, start + 4)[0]
        end = start + 8 + length
        if end > len(data) or data[start + 8:start + 12] != b'ObjD':
            raise ValueError('invalid FEng object enclosure')
        object_size = struct.unpack_from('<I', data, start + 12)[0]
        at, object_end = start + 16, start + 16 + object_size
        if object_end > end:
            raise ValueError('FEng fields outside object')
        fields = []
        while at < object_end:
            if at + 4 > object_end:
                raise ValueError('truncated FEng field')
            tag, size = data[at:at+2], struct.unpack_from('<H', data, at+2)[0]
            next_at = at + 4 + size
            if next_at > object_end:
                raise ValueError('FEng field outside object')
            fields.append((tag, at, data[at+4:next_at]))
            at = next_at
        names = [value.split(b'\0', 1)[0].decode('ascii') for tag, _, value in fields if tag == b'On']
        if len(names) != 1:
            raise ValueError('missing or duplicate FEng object name')
        name = names[0]
        if not any(part in name.lower() for part in name_parts):
            continue
        row = dict(name=name, offset=start, fields=[])
        for tag, offset, value in fields:
            item = dict(tag=tag.decode('ascii'), offset=offset, bytes=len(value), raw=value.hex())
            if tag == b'OP' and len(value) == 16:
                item['resource_index'] = struct.unpack_from('<H', value, 12)[0]
            if tag == b'SH' and len(value) == 4:
                item['string_hash'] = '%08X' % struct.unpack('<I', value)[0]
            if tag == b'Sj' and len(value) == 4:
                item['justification'] = struct.unpack('<I', value)[0]
            if tag == b'St':
                item['text'] = value.decode('utf-16-le').rstrip('\0')
            if tag == b'SA' and len(value) >= 64:
                item['bgra'] = struct.unpack_from('<4I', value)
                item['position_depth'] = struct.unpack_from('<3f', value, 0x1C)
                item['size_scale'] = struct.unpack_from('<2f', value, 0x38)
            row['fields'].append(item)
        result.append(row)
    return result


def resources(data):
    rows = []
    for match in re.finditer(rb'Res\xcc', data):
        at = match.start()
        if at + 8 > len(data):
            raise ValueError('truncated resource enclosure')
        end = at + 8 + struct.unpack_from('<I', data, at+4)[0]
        if end > len(data):
            raise ValueError('resources outside package')
        row = dict(offset=at, chunks=[])
        cursor = at + 8
        while cursor < end:
            if cursor + 8 > end:
                raise ValueError('truncated resource child')
            size = struct.unpack_from('<I', data, cursor+4)[0]
            next_at = cursor + 8 + size
            if next_at > end:
                raise ValueError('resource child outside enclosure')
            item = dict(tag=data[cursor:cursor+4].hex(), offset=cursor, bytes=size)
            payload = data[cursor+8:next_at]
            if data[cursor:cursor+4] == b'RsNm':
                item['names'] = [{'offset': cursor+8+value.start(),
                                  'text': value.group()[:-1].decode('ascii')}
                                 for value in re.finditer(rb'[ -~]+\0', payload)]
            else:
                item['raw'] = payload.hex()
                if data[cursor:cursor+4] == b'RsRq':
                    if size < 4:
                        raise ValueError('truncated resource count')
                    count = struct.unpack_from('<I', payload)[0]
                    if size != 4 + count * 24:
                        raise ValueError('invalid resource record count')
                    item['records'] = [dict(index=index, offset=cursor+12+index*24,
                                            words=struct.unpack_from('<6I', payload, 4+index*24))
                                       for index in range(count)]
            row['chunks'].append(item)
            cursor = next_at
        names = [child for child in row['chunks'] if child['tag'] == b'RsNm'.hex()]
        requests = [child for child in row['chunks'] if child['tag'] == b'RsRq'.hex()]
        if len(names) != 1 or len(requests) != 1:
            raise ValueError('missing or duplicate resource tables')
        table = {entry['offset'] - names[0]['offset'] - 8: entry['text']
                 for entry in names[0]['names']}
        for entry in requests[0]['records']:
            if entry['words'][1] not in table:
                raise ValueError('resource name offset is not a string boundary')
            entry['name'] = table[entry['words'][1]]
            entry['type'] = entry['words'][2]
        rows.append(row)
    return rows


def output_path(value, roots):
    out = value.resolve()
    for root in roots:
        if out == root or root in out.parents or out in root.parents:
            raise ValueError('output overlaps input')
    # Existing ancestors may be non-ASCII; all new components must be ASCII.
    probe = out
    while not probe.exists():
        if any(ord(char) < 32 or ord(char) > 126 for char in probe.name):
            raise ValueError('non-ASCII output component')
        probe = probe.parent
    if out.exists():
        raise ValueError('output already exists')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source', help='full source commit SHA recorded with the reports')
    parser.add_argument('--native', action='store_true', help='execute bounded pure retail consumers in Unicorn 2')
    args = parser.parse_args()
    if args.source is not None and not re.fullmatch('[0-9a-f]{40}', args.source):
        parser.error('source must be a full lowercase commit SHA')
    roots = [args.root.resolve()]
    if args.reference:
        roots.append(args.reference.resolve())
    if any(not root.is_dir() for root in roots):
        parser.error('input directory missing')
    out = output_path(args.out, roots)
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    out.mkdir(parents=True)
    before = [manifest(root) for root in roots]
    for index, value in enumerate(before):
        (out / ('before-%d.json' % index)).write_bytes(value)
    try:
        data = (roots[0] / 'speed.exe').read_bytes()
        if sha(data) != EXE_SHA:
            raise ValueError('unsupported executable')
        image = PE(data)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        report = []
        for label, start, end in RANGES + CONTEXT:
            blob = image.read(start, end - start)
            report.append('\n%s %08X..%08X SHA256 %s' % (label, start, end, sha(blob)))
            decoded = list(md.disasm(blob, start))
            if label not in {item[0] for item in CONTEXT} and sum(i.size for i in decoded) != len(blob):
                raise ValueError('incomplete function decoding: ' + label)
            report.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str) for i in decoded)
        (out / 'instructions.txt').write_text('\n'.join(report) + '\n', encoding='ascii')
        bun = (roots[0] / 'Global/INGAMEB.BUN').read_bytes()
        if sha(bun) != BUNDLE_SHA:
            raise ValueError('unsupported HUD bundle')
        package = jdlz(bun[0x16F40C:])
        if b'HUD_SingleRace.fng' not in package[:256]:
            raise ValueError('unexpected package')
        strings = []
        for match in re.finditer(rb'[ -~]{4,}\0', package):
            text = match.group()[:-1].decode('ascii')
            if any(part in text.lower() for part in ('lap', 'time', 'current', 'best', 'speed_counter',
                                                     'font', 'conduit', 'lcd', 'arial')):
                strings.append(dict(offset=match.start(), text=text,
                                    context=package[max(0, match.start()-48):match.end()+144].hex()))
        shapes = []
        for match in re.finditer(rb'SA[DT]\0', package):
            at = match.start()
            if at + 0x44 > len(package):
                raise ValueError('truncated shape')
            shapes.append(dict(offset=at, kind=match.group()[:3].decode('ascii'),
                               bgra=struct.unpack_from('<4I', package, at + 4),
                               position_depth=struct.unpack_from('<3f', package, at + 0x20),
                               size_scale=struct.unpack_from('<2f', package, at + 0x3C),
                               header=package[at:at+0x50].hex()))
        language = (roots[0] / 'Languages/English.bin').read_bytes()
        if sha(language) != LANGUAGE_SHA:
            raise ValueError('unsupported language file')
        pointers = (0x6C735C, 0x6C7350, 0x6C7344, 0x6C7334, 0x6C7324,
                    0x6C7310, 0x6C7308, 0x6C72F0, 0x6C72E8, 0x6C72DC,
                    0x6C7968, 0x6CC568)
        constants = [{'va': '%08X' % at, 'text': image.read(at, 80).split(b'\0', 1)[0].decode('ascii')}
                     for at in pointers]
        resource_tables = resources(package)
        objects = feng_objects(package)
        if len(resource_tables) != 1:
            raise ValueError('expected one resource table')
        requests = next(child['records'] for child in resource_tables[0]['chunks']
                        if child['tag'] == b'RsRq'.hex())
        for obj in objects:
            fields = [field for field in obj['fields'] if field['tag'] == 'OP']
            if len(fields) != 1 or fields[0]['bytes'] != 16:
                raise ValueError('missing or unsupported OP field')
            index = struct.unpack_from('<H', bytes.fromhex(fields[0]['raw']), 12)[0]
            if index != 0xFFFF:
                if index >= len(requests):
                    raise ValueError('object resource index outside table')
                obj['default_resource'] = requests[index]
        result = dict(source_commit=args.source, probe_sha256=sha(Path(__file__).read_bytes()),
                      executable_sha256=sha(data), package_sha256=sha(package),
                      bundle_sha256=sha(bun), language_sha256=sha(language),
                      strings=strings, shapes=shapes, fonts=fonts(language),
                      objects=objects, constants=constants, resources=resource_tables)
        (out / 'data.json').write_text(json.dumps(result, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
        if args.native:
            from hud254_native import measure
            native = measure(image)
            native['source_commit'] = args.source
            native['oracle_sha256'] = sha((Path(__file__).parent/'hud254_native.py').read_bytes())
            (out / 'native.json').write_text(json.dumps(native, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    finally:
        after = [manifest(root) for root in roots]
        for index, value in enumerate(after):
            (out / ('after-%d.json' % index)).write_bytes(value)
        if after != before:
            raise RuntimeError('input files/directories changed')
    label = 'retail/reference' if args.reference else 'retail'
    print('PASS: bounded HUD trace; full %s manifests unchanged' % label)


if __name__ == '__main__':
    main()
