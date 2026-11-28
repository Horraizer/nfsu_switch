"""Read-only GNDE69 CalculateH and GetPixelSize shadow witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.ppc_const import (UC_PPC_REG_1, UC_PPC_REG_3, UC_PPC_REG_4,
                               UC_PPC_REG_FPR0, UC_PPC_REG_FPR1, UC_PPC_REG_MSR,
                               UC_PPC_REG_PC)


ELF_SHA256 = 'dffc823164d98094e3f32a76f61ad22c7fc2b6375f8b99df5df78e718a70a4fd'


def word(data, at):
    return struct.unpack_from('>I', data, at)[0]


def load_disc(path):
    with path.open('rb') as image:
        def read(at, size):
            image.seek(at)
            value = image.read(size)
            if len(value) != size:
                raise ValueError('truncated original disc')
            return value
        header = read(0, 0x440)
        assert header[:8] == b'GNDE69\0\0'
        boot, fst_at, fst_size = struct.unpack_from('>3I', header, 0x420)
        fst = read(fst_at, fst_size)
        count = word(fst, 8)
        names = fst[12 * count:]
        found = []
        for at in range(12, 12 * count, 12):
            kind, offset, size = struct.unpack_from('>3I', fst, at)
            name_at = kind & 0xffffff
            name = names[name_at:names.index(0, name_at)]
            if kind >> 24 == 0 and name.lower() == b'speed.elf':
                found.append(read(offset, size))
        assert len(found) == 1
        elf = found[0]
        assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
        assert elf[:6] == b'\x7fELF\x01\x02'
        dol = read(boot, 0x100)
        dol_segments = []
        for i in range(18):
            size = word(dol, 0x90 + 4 * i)
            if size:
                dol_segments.append((word(dol, 0x48 + 4 * i), read(boot + word(dol, 4 * i), size)))
    at = word(elf, 32)
    stride, count, strings = struct.unpack_from('>3H', elf, 46)
    sections = [struct.unpack_from('>10I', elf, at + stride * i) for i in range(count)]
    names_section = sections[strings]
    names = elf[names_section[4]:names_section[4] + names_section[5]]
    loaded = []
    matched = []
    for sec in sections:
        if sec[1] == 8 or not sec[5] or not 0x80000000 <= sec[3] < 0x82000000:
            continue
        name = names[sec[0]:names.index(0, sec[0])].decode('ascii')
        data = elf[sec[4]:sec[4] + sec[5]]
        loaded.append((sec[3], data))
        if name in ('.init', '.text', '.rodata', '.data', '.sdata', '.sdata2'):
            assert any(data == blob[sec[3] - start:sec[3] - start + len(data)]
                       for start, blob in dol_segments if start <= sec[3] and sec[3] + len(data) <= start + len(blob))
            matched.append(name)
    assert len(matched) == 6 and word(dol, 0xe0) == word(elf, 24)
    return loaded, matched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--full', action='store_true', help='Check every uint16 FOV word instead of the small pinned sample')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    for output in (args.output, args.evidence):
        try:
            relative = output.resolve().relative_to(repo)
        except ValueError:
            parser.error('outputs must stay inside this checkout')
        if not relative.parts or not relative.as_posix().isascii() or output.exists():
            parser.error('fresh ASCII output paths required')
        if output.resolve() == args.disc.resolve() or args.disc.resolve().parent in output.resolve().parents:
            parser.error('outputs must stay outside the original disc directory')
    loaded, matched = load_disc(args.disc)
    gc = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
    gc.mem_map(0x80000000, 0x02000000)
    for address, data in loaded:
        gc.mem_write(address, data)
    gc.reg_write(UC_PPC_REG_MSR, gc.reg_read(UC_PPC_REG_MSR) | 0x2000)
    conversions = 0
    allowed = ((0x80005668, 0x800056ac), (0x80167a28, 0x80167ae4),
               (0x80167b20, 0x80167b48), (0x80163908, 0x80163a08))

    def guard(uc, address, size, unused):
        nonlocal conversions
        if not any(start <= address and address + size <= end for start, end in allowed):
            raise RuntimeError('original escaped declared ranges: %08x' % address)
        if address == 0x80167a40:
            # Unicorn lacks Gekko psq_l. This exact instruction loads the
            # ushort angle just stored by bSin, using the original GQR type.
            assert bytes(uc.mem_read(address, size)) == bytes.fromhex('e001b008')
            stack = uc.reg_read(UC_PPC_REG_1)
            angle = struct.unpack('>H', uc.mem_read(stack + 8, 2))[0]
            uc.reg_write(UC_PPC_REG_FPR0, struct.unpack('>Q', struct.pack('>d', float(angle)))[0])
            uc.reg_write(UC_PPC_REG_PC, address + size)
            conversions += 1
    gc.hook_add(UC_HOOK_CODE, guard)

    def scale(fov):
        gc.reg_write(UC_PPC_REG_1, 0x817e0000)
        gc.reg_write(UC_PPC_REG_3, fov)
        gc.emu_start(0x80005668, 0x800056ac, count=1000)
        value = struct.unpack('>d', struct.pack('>Q', gc.reg_read(UC_PPC_REG_FPR1)))[0]
        return struct.unpack('>I', struct.pack('>f', value))[0]

    fovs = sorted(set(range(0, 65536, 257)) | {0, 1, 2, 1634, 4096, 8191, 8192, 10000,
        10001, 13019, 13020, 13021, 16383, 16384, 16385, 20000, 20001, 24000, 32767, 32768, 65535})
    if args.full:
        fovs = range(65536)
    rows = [dict(fov=fov, scale_bits=scale(fov)) for fov in fovs]
    view, point, camera = 0x81000000, 0x81001000, 0x81002000
    gc.mem_write(view + 0x34, struct.pack('>I', camera))
    gc.mem_write(camera + 0x40, struct.pack('>3f', 0, 0, 0))
    gc.mem_write(camera + 0x50, struct.pack('>3f', 1, 0, 0))
    boundary = dict(fov=16384, position=20.066667556762695, scale_bits=scale(16384))
    gc.mem_write(view + 0xc, struct.pack('>I', boundary['scale_bits']))
    gc.mem_write(point, struct.pack('>3f', boundary['position'], 0, 0))
    gc.reg_write(UC_PPC_REG_3, view)
    gc.reg_write(UC_PPC_REG_4, point)
    gc.reg_write(UC_PPC_REG_FPR1, struct.unpack('>Q', struct.pack('>d', 3.0))[0])
    gc.emu_start(0x80163908, 0x80163a08, count=1000)
    boundary['pixels'] = gc.reg_read(UC_PPC_REG_3)
    assert boundary['pixels'] == 45
    lines = ['// Generated by tools/car_shadow_gc_native.py; original GNDE69 revision0.',
             'struct ShadowGcScaleNative {unsigned fov;std::uint32_t scale;};',
             'static const ShadowGcScaleNative shadow_gc_scale_native[] = {']
    lines += ['    {%d,0x%08xU},' % (row['fov'], row['scale_bits']) for row in rows]
    lines += ['};', 'static constexpr unsigned shadow_gc_boundary_fov = %d;' % boundary['fov'],
              'static constexpr double shadow_gc_boundary_position = %s;' % repr(boundary['position']),
              'static constexpr int shadow_gc_boundary_pixels = %d;' % boundary['pixels'], '']
    args.output.write_text('\n'.join(lines), encoding='ascii')
    args.evidence.write_text(json.dumps(dict(elf_sha256=ELF_SHA256, matched_dol_sections=matched,
        scope='Original CalculateH/bSin/bCos and one original GetPixelSize LOD boundary; only psq_l ushort load emulated.',
        conversions=conversions, scales=rows, boundary=boundary), indent=2) + '\n', encoding='ascii')
    print('PASS original GC shadow scale: %d FOV words, GetPixelSize LOD boundary; only psq_l angle loads emulated' % len(rows))


if __name__ == '__main__':
    main()
