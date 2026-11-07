#!/usr/bin/env python3
"""Read-only authentication of the installed NA startup movies. No asset output."""
import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path
sys.dont_write_bytecode = True
from event0_trace import jdlz


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('retail_root', type=Path)
    root = parser.parse_args().retail_root
    exe = (root / 'speed.exe').read_bytes()
    image = jdlz((root / 'FrontEnd' / 'FrontB.lzc').read_bytes())
    # The authenticated installation's PE section raw offsets equal RVAs.
    names = []
    for address in range(0x6FA1A4, 0x6FA1BC, 4):
        offset = struct.unpack_from('<I', exe, address - 0x400000)[0] - 0x400000
        names.append(exe[offset:exe.index(b'\0', offset)].decode('ascii'))
    expected = ['DiscErrorPC.fng', 'LS_EAlogo.fng', 'LS_PSAMovie.fng',
                'LS_IntroFMV.fng', 'LS_Splash_PC.fng', 'MU_UG_NEwOrLoad_PC2.fng']
    if names != expected:
        raise ValueError('unsupported executable bootflow')
    if exe[0xDEAE1:0xDEAE6] != bytes.fromhex('b8103b6c00'):
        raise ValueError('EA logo constructor differs')
    if exe[0xDEC18:0xDEC1D] != bytes.fromhex('68fc3a6c00'):
        raise ValueError('PSA constructor differs')
    # Native type7 Movie draw: object geometry controls vertices. Texture
    # dimensions subsequently control only UV extent/backing texture padding.
    for address, code in [(0x4F2D2D, '565357e84bf0ffff'),
                          (0x4F1AE2, 'd84f38'), (0x4F1B26, 'd84f3c'),
                          (0x4F1E29, 'c74424600000a043'),
                          (0x4F1E31, 'c744246400007043')]:
        offset = address - 0x400000
        if exe[offset:offset + len(bytes.fromhex(code))] != bytes.fromhex(code):
            raise ValueError('native Movie geometry differs at 0x%X' % address)
    # Original MAD decoder reads the complete interval dword at offset 12.
    # Its denominator is the native constant 65535, not a highword 33ms.
    if struct.unpack_from('<f', exe, 0x6CCD08 - 0x400000)[0] != 65535.0:
        raise ValueError('native MAD timebase denominator differs')
    if exe[0x2490C8:0x2490CA] != bytes.fromhex('6a04') or exe[0x2490E5:0x2490F1] != bytes.fromhex('d83508cd6c00d83d44c86c00'):
        raise ValueError('native MAD interval conversion differs')
    if struct.unpack_from('<I', exe, 0x6A9854 + 16 - 0x400000)[0] != 0x648FC9 or exe[0x248FD3:0x248FD6] != bytes.fromhex('d94004'):
        raise ValueError('native MAD framerate vtable/getter differs')
    print('native_mad_duration_units_per_ms=65535 header_offset=12 bytes=4')
    movie_objects = {b'LS_EAlogo.fng\0': 0x25C,
                     b'LS_PSAMovie.fng\0': 0x248,
                     b'LS_IntroFMV.fng\0': 0x3E8}
    found_objects = set()
    found = False
    for match in re.finditer(b'JDLZ', image):
        offset = match.start()
        if len(image) - offset < 16:
            continue
        size = struct.unpack_from('<I', image, offset + 12)[0]
        try:
            package = jdlz(image[offset:offset + size])
        except ValueError:
            continue
        for name, size_offset in movie_objects.items():
            if name in package:
                display = struct.unpack_from('<ff', package, size_offset)
                if display != (640.0, 480.0):
                    raise ValueError('original Movie Size differs: ' + name.decode('ascii'))
                found_objects.add(name)
                print('%s package=0x%X Size=(640,480)' % (name[:-1].decode('ascii'), offset))
        if b'LS_IntroFMV.fng\0' in package:
            if package[0x304:0x30D] != b'e3_title\0':
                raise ValueError('intro FNG movie differs')
            print('intro_fng_offset=0x%X object=e3_title' % offset)
            found = True
    if found_objects != set(movie_objects):
        raise ValueError('original Movie geometry missing')
    if not found:
        raise ValueError('intro FNG missing')
    print('bootflow=' + ' -> '.join(names))
    for relative in ['speed.exe', 'FrontEnd/FrontB.lzc', 'Movies/na_boot.mad',
                     'Movies/PSA.mad', 'Movies/e3_title.mad']:
        data = (root / relative).read_bytes()
        print('%s bytes=%d sha256=%s' % (relative, len(data), hashlib.sha256(data).hexdigest()))
    print('PASS original NA introduction identity')


if __name__ == '__main__':
    main()
