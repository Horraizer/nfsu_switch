"""Rebuild ASCII selector1 original angle lookup data from authenticated retail."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,manifest
from handling_brake_compare import EXE_SHA


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();before=manifest(root)
    try:
        data=(root/'speed.exe').read_bytes()
        if hashlib.sha256(data).hexdigest()!=EXE_SHA:raise ValueError('unsupported executable')
        raw=Image(data).read(0x6F2280,208*8)
        lines=['#pragma once','#include <cstdint>','namespace nfsu {',
               '// Original6F2280..6F2900: 208 angle/slope records, eight bytes each.',
               '// SHA256 '+hashlib.sha256(raw).hexdigest(),
               'struct PlayerAngleSample { std::uint16_t angle; std::uint32_t slope_bits; };',
               'inline constexpr PlayerAngleSample kPlayerAngleSamples[208] = {']
        for i in range(208):
            angle,padding,slope=struct.unpack_from('<HHI',raw,8*i)
            if padding:raise ValueError('unexpected lookup padding')
            lines.append('    {%dU, 0x%08XU},'%(angle,slope))
        lines+=['};','} // namespace nfsu','']
        target=Path.cwd()/'include/nfsu/player_angle_data.h'
        target.write_bytes('\n'.join(lines).encode('ascii'))
        print('PASS: rebuilt208 authenticated original angle records as ASCII')
    finally:
        if before!=manifest(root):raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__':main()
