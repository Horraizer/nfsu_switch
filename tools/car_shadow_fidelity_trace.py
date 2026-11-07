"""Bounded read-only shadow follow-up trace for issue 267 (Capstone 5)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from car_shadow_trace import main as prior_main


IDENTITIES = {
    'speed.exe': (3178496, 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'),
    'Cars/240SX/TEXTURES.BIN': (697340, '725dc56cb4807ff8e8e96f1d667f5fdf8dc5ade3c7d06e759fc62e7deefdf6ba'),
    'fx/world.efx': (2404, 'd1cdbd788d564c4cd408a060d0eda4e99245f0eb54b18a4fcee848d2966e6c98'),
    'Global/GLOBALB.BUN': (2361176, '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'),
    'Cars/240SX/GEOMETRY.BIN': (4780816, '002202192752838e1b65e9b7c219eab381fd5f916c3d031dde93428eb325f619'),
}

# Known entry points or boundaries, not a whole-image speculative disassembly.
RANGES = [
    (0x56A130, 0x56A3B0), (0x567890, 0x567940),
    (0x56A8E0, 0x56ABEF), (0x540004, 0x540023),
    (0x56CE90, 0x56CF10), (0x56DC30, 0x56DEEC),
    (0x56E330, 0x56E455), (0x56EB0F, 0x56F475),
    (0x440A40, 0x440C80), (0x5ABDAF, 0x5ABE40),
    (0x56ABEF, 0x56ACF0), (0x56B08A, 0x56B0D4),
    (0x40F3D0, 0x40F580), (0x408B20, 0x408D20),
    (0x40DB20, 0x40DBC0),
]


def manifest(root):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        stat = path.stat()
        relative = path.relative_to(root).as_posix()
        if path.is_file():
            rows.append([relative, 'file', stat.st_size, stat.st_mtime_ns,
                         hashlib.sha256(path.read_bytes()).hexdigest()])
        elif path.is_dir():
            rows.append([relative, 'directory', stat.st_mtime_ns])
        else:
            raise ValueError('Unsupported retail entry: ' + ascii(relative))
    return json.dumps(rows, ensure_ascii=True, separators=(',', ':')).encode('ascii')


def trace(root):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    for name, (size, digest) in IDENTITIES.items():
        data = (root / name).read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if (len(data), actual) != (size, digest):
            raise ValueError('Unsupported input: ' + name)
        print('INPUT', name, len(data), actual)
    data = (root / 'speed.exe').read_bytes()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for start, end in RANGES:
        print('RANGE 0x%08x..0x%08x' % (start, end))
        for insn in md.disasm(data[start-0x400000:end-0x400000], start):
            print('0x%08x %-9s %s' % (insn.address, insn.mnemonic, insn.op_str))
    for va in [0x6B5D80, 0x6B5D84, 0x6B5D88, 0x6B5DB4, 0x6B5DB8,
               0x6B5DBC, 0x6B5DC0, 0x6B5DC8, 0x6B5DCC, 0x6B5D58,
               0x6CC7DC, 0x6CC7BC, 0x6CC8A8, 0x6F1AD4,
               0x6B5D68, 0x6B5E04, 0x6CC7B8]:
        print('F32 0x%08x %.9g' % (va, struct.unpack_from('<f', data, va-0x400000)[0]))
    for va in [0x6BBD0C, 0x6BBD14, 0x6BBD20]:
        start = va - 0x400000
        print('STRING 0x%08x' % va, data[start:data.index(b'\0', start)].decode('ascii'))
    print('SELECTED_LIFT_INDICES', [i for i in range(16) if ((i >> 1) ^ i) & 5 == 5])
    print('WHEEL_FALLBACK_MAP', [2, 2, 1, 1, 2, 2, 1, 1, 3, 3, 0, 0, 3, 3, 0, 0])
    geometry = (root / 'Cars/240SX/GEOMETRY.BIN').read_bytes()
    def walk(begin, end, depth=0):
        if depth > 8:
            raise ValueError('Chunk depth exceeded')
        while begin < end:
            if begin + 8 > end:
                raise ValueError('Truncated chunk header')
            kind, size = struct.unpack_from('<II', geometry, begin)
            payload, stop = begin + 8, begin + 8 + size
            if stop > end:
                raise ValueError('Chunk exceeds parent')
            if kind & 0x80000000:
                yield from walk(payload, stop, depth + 1)
            elif kind == 0x134011:
                aligned = (payload + 15) & ~15
                if stop - aligned != 176:
                    raise ValueError('Unexpected solid header size')
                name = geometry[stop-28:stop].split(b'\0')[0].decode('ascii')
                if name.endswith('_A') and ('KIT00' in name or name == '240SX_BASE_A'
                                           or 'MIRROR' in name):
                    low = struct.unpack_from('<3f', geometry, aligned + 0x20)
                    high = struct.unpack_from('<3f', geometry, aligned + 0x30)
                    print('SOLID_BOUNDS', name, 'header=0x%x' % aligned,
                          'min=', low, 'max=', high)
            begin = stop
    list(walk(0, len(geometry)))
    source = bytes(v ^ 0xAA for v in (root / 'fx/world.efx').read_bytes())
    for number, line in enumerate(source.splitlines(), 1):
        if any(t in line for t in [b'sampler', b'Filter', b'Address', b'tex2D(',
                                  b'Texture', b'CullMode', b'Modulate2x']):
            print('world.efx:%d' % number, line.decode('ascii').strip())
    print('PASS: bounded follow-up trace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--prior', action='store_true', help='Also reproduce issue 188 trace')
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if root == out or root in out.parents:
        parser.error('--out must be outside retail')
    if out.exists():
        parser.error('--out must be fresh')
    if not root.is_dir():
        parser.error('--root must exist')
    out.mkdir(parents=True)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        import contextlib
        import sys
        with (out / 'trace.txt').open('w', encoding='ascii', newline='\n') as output:
            with contextlib.redirect_stdout(output):
                if args.prior:
                    old_argv = sys.argv
                    try:
                        sys.argv = ['car_shadow_trace.py', '--root', str(root), '--out', str(out / 'prior')]
                        prior_main()
                    finally:
                        sys.argv = old_argv
                trace(root)
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        print('RETAIL FILE/DIRECTORY MANIFEST EQUAL', len(json.loads(after)), 'entries')
    print('PASS: trace saved; retail unchanged')


if __name__ == '__main__':
    main()
