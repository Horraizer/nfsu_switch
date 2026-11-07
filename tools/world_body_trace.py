"""Issue 259: bounded read-only WorldObjectCollisionBody research.

Requires Python 3.8+ and Capstone. Reports are static source observations,
not a named event0 instance, runtime contact trace or production solver.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


INPUTS = {
    'speed.exe': (3178496, 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'),
    'Tracks/TRACKB1003.lzc': (433352, 'ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA'),
    'Tracks/STREAML1RA.BUN': (92394688, '2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E'),
}

# Traced entry/instruction boundaries; no linear sweep through compiler padding.
# A range may be a bounded slice rather than a complete function.
RANGES = [
    ('base-kind-store', 0x468340, 0x46835C),
    ('WorldObject-constructor', 0x578060, 0x578282),
    ('collision-type-factory', 0x578590, 0x578660),
    ('generic-kind4-constructor', 0x592280, 0x592437),
    ('BB-kind4-constructor', 0x592470, 0x59260D),
    ('pair-eligibility', 0x5927D0, 0x5927DB),
    ('inverse-mass', 0x5927E0, 0x59281C),
    ('inverse-inertia', 0x592820, 0x592846),
    ('position', 0x592850, 0x59287E),
    ('vfunc-0C', 0x592880, 0x5928B3),
    ('vfunc-10', 0x5928C0, 0x5928D6),
    ('vfunc-14', 0x5928E0, 0x5928FC),
    ('vfunc-18', 0x592900, 0x592933),
    ('apply-impulse', 0x592940, 0x5929B9),
    ('pose-write', 0x5929C0, 0x592A4C),
    ('separate-array-loader', 0x592A50, 0x592AAE),
    ('BB-bound-reader', 0x53F5F0, 0x53F658),
    ('box-shape', 0x473CF0, 0x473D60),
    ('zero-return', 0x575940, 0x575943),
    ('base-serialization', 0x4688E0, 0x468A41),
    ('rigid-property-initialization', 0x584B10, 0x584B72),
    ('Smokeable-backing-flag', 0x59686F, 0x59688E),
]


def digest_file(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest().upper()


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
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE signature')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optsize = struct.unpack_from('<H', data, pe + 20)[0]
        if struct.unpack_from('<H', data, pe + 24)[0] != 0x10B:
            raise ValueError('expected PE32')
        base = struct.unpack_from('<I', data, pe + 52)[0]
        self.sections = []
        for index in range(count):
            at = pe + 24 + optsize + 40 * index
            _, va, size, raw = struct.unpack_from('<IIII', data, at + 8)
            if raw + size > len(data):
                raise ValueError('PE raw section exceeds file')
            self.sections.append((base + va, size, raw))

    def read(self, va, size):
        for start, length, raw in self.sections:
            if start <= va and va + size <= start + length:
                return self.data[raw + va - start:raw + va - start + size]
        raise ValueError('VA outside raw PE sections: %08X' % va)


def jdlz(blob):
    # Format: docs/FORMATS.md. Decode in memory; never export asset bytes.
    if blob[:8] != b'JDLZ\x02\x10\0\0':
        raise ValueError('unsupported JDLZ header')
    target, compressed = struct.unpack_from('<II', blob, 8)
    if compressed != len(blob) or target > 16 * 1024 * 1024:
        raise ValueError('unsupported JDLZ sizes')
    decoded = bytearray()
    pos, flags1, flags2 = 16, 1, 1
    while len(decoded) < target:
        if flags1 == 1:
            flags1 = blob[pos] | 0x100
            pos += 1
        if flags2 == 1:
            flags2 = blob[pos] | 0x100
            pos += 1
        if flags1 & 1:
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
            decoded.append(blob[pos])
            pos += 1
        flags1 >>= 1
    return bytes(decoded)


def chunks(blob, begin=0, end=None, depth=0):
    if end is None:
        end = len(blob)
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


def census(root):
    rows = []
    for directory in ('Global', 'Tracks', 'FrontEnd'):
        for path in sorted((root / directory).iterdir()):
            if not path.is_file() or not path.stat().st_size:
                continue
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest().upper()
            size = len(data)
            compressed = data[:4] == b'JDLZ'
            if compressed:
                data = jdlz(data)
            count, arrays = 0, []
            for tag, offset, end in chunks(data):
                count += 1
                if tag in (0x34300, 0x34301):
                    stride = 68 if tag == 0x34300 else 180
                    length = end - offset - 8
                    if length % stride:
                        raise ValueError('separate loader array has partial record')
                    arrays.append(dict(tag='%08X' % tag, offset=offset,
                                       records=length // stride, stride=stride))
            rows.append(dict(file=path.relative_to(root).as_posix(), bytes=size,
                             sha256=digest, jdlz=compressed, chunks=count,
                             separate_loader_arrays=arrays))
    return rows


def trace(image):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    lines = ['Static source trace only; no runtime process/contact measured.']
    for name, start, end in RANGES:
        data = image.read(start, end - start)
        decoded = list(md.disasm(data, start))
        if sum(i.size for i in decoded) != len(data):
            raise ValueError('incomplete instruction range: ' + name)
        lines.append('\n%s %08X..%08X SHA-256 %s' %
                     (name, start, end, hashlib.sha256(data).hexdigest().upper()))
        lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str) for i in decoded)
    table = []
    for offset in range(0, 0x78, 4):
        va = struct.unpack('<I', image.read(0x6B8D88 + offset, 4))[0]
        table.append(dict(offset='%02X' % offset, target='%08X' % va))
    constants = {}
    for va in (0x6CC7A4, 0x6CC7BC, 0x6B5AA8, 0x6CC7DC):
        raw = image.read(va, 4)
        constants['%08X' % va] = dict(f32=struct.unpack('<f', raw)[0],
                                     u32=struct.unpack('<I', raw)[0])
    if image.read(0x6BA6B4, 3) != b'BB\0':
        raise ValueError('unexpected BB tag')
    return lines, dict(vtable=table, constants=constants, collision_tag='BB',
                      named_event0_instance='unavailable',
                      kind4_backing_gate_writer='unavailable',
                      runtime_contact_order='unavailable')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--census', action='store_true',
                        help='also inspect all nonempty immediate Global/Tracks/FrontEnd files')
    args = parser.parse_args()
    workspace = Path.cwd().resolve()
    # Windows Python 3.8 can leave resolve() relative if a parent is missing.
    # Anchor CLI inputs before resolving so fresh output parents work too.
    root = (workspace / args.root).resolve()
    out = (workspace / args.out).resolve()
    protected = Path('E:/Games').resolve()
    if (out == root or root in out.parents or out in root.parents or
            out == protected or protected in out.parents):
        parser.error('output overlaps retail root or protected E:/Games')
    if workspace not in out.parents:
        parser.error('output must be inside the current workspace')
    if not out.relative_to(workspace).as_posix().isascii():
        parser.error('output relative path must be ASCII')
    if out.exists():
        parser.error('output directory must be fresh')
    if not root.is_dir():
        parser.error('retail root must exist')
    before = manifest(root)
    out.mkdir(parents=True, exist_ok=False)
    (out / '259-before.json').write_bytes(before)
    try:
        inputs = []
        for relative, (expected_size, expected_hash) in INPUTS.items():
            path = root / relative
            if path.stat().st_size != expected_size or digest_file(path) != expected_hash:
                raise ValueError('unsupported input identity: ' + relative)
            inputs.append(dict(file=relative, bytes=expected_size, sha256=expected_hash))
        lines, facts = trace(Image((root / 'speed.exe').read_bytes()))
        facts['inputs'] = inputs
        if args.census:
            facts['census'] = census(root)
            print('CENSUS: files=%d separate_loader_arrays=%d' %
                  (len(facts['census']), sum(len(x['separate_loader_arrays']) for x in facts['census'])))
        (out / '259-instructions.txt').write_text('\n'.join(lines) + '\n', encoding='ascii')
        (out / '259-source.json').write_text(json.dumps(facts, ensure_ascii=True, indent=2) + '\n', encoding='ascii')
        print('PASS: pinned inputs, PE mapping and complete bounded instruction ranges')
    finally:
        after = manifest(root)
        (out / '259-after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('PASS: retail root/directory/file/mtime/SHA-256 manifests identical')


if __name__ == '__main__':
    main()
