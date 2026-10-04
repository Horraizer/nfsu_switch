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
        if b'LS_IntroFMV.fng\0' in package:
            if package[0x304:0x30D] != b'e3_title\0':
                raise ValueError('intro FNG movie differs')
            print('intro_fng_offset=0x%X object=e3_title' % offset)
            found = True
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
