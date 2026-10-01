"""Bounded read-only issue 253 HUD/RPM trace; requires Capstone 5.

Outputs ASCII disassembly and FEng fields to a fresh directory outside retail.
The instruction ranges are evidence, not an assertion of runtime scheduling.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from handling_measure import manifest
from race_replay_check import retail_manifest


def jdlz(data):
    if data[:8] != b'JDLZ\x02\x10\0\0':
        raise ValueError('unexpected JDLZ header')
    target, size = struct.unpack_from('<II', data, 8)
    data = data[:size]
    at, f1, f2, result = 16, 1, 1, bytearray()
    while len(result) < target:
        if f1 == 1:
            f1, at = data[at] | 256, at + 1
        if f2 == 1:
            f2, at = data[at] | 256, at + 1
        if f1 & 1:
            a, b = data[at:at+2]
            at += 2
            if f2 & 1:
                length, distance = (b | ((a & 240) << 4)) + 3, (a & 15) + 1
            else:
                length, distance = (a & 31) + 3, (b | ((a & 224) << 3)) + 17
            f2 >>= 1
            if distance > len(result) or length > target - len(result):
                raise ValueError('invalid JDLZ back-reference')
            for _ in range(length):
                result.append(result[-distance])
        else:
            result.append(data[at])
            at += 1
        f1 >>= 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents:
        parser.error('output must be outside retail')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root), retail_manifest(root)
    (out / 'before.json').write_bytes(before[0])
    try:
        data = (root / 'speed.exe').read_bytes()
        sha = hashlib.sha256(data).hexdigest().upper()
        if sha != 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7':
            raise ValueError('unsupported speed.exe')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe+4] != b'PE\0\0':
            raise ValueError('not a PE')
        count = struct.unpack_from('<H', data, pe+6)[0]
        optsize = struct.unpack_from('<H', data, pe+20)[0]
        base = struct.unpack_from('<I', data, pe+52)[0]
        sections = [struct.unpack_from('<IIII', data, pe+24+optsize+i*40+8)
                    for i in range(count)]

        def read(va, size):
            for _, rva, rawsize, raw in sections:
                if base+rva <= va and va+size <= base+rva+rawsize:
                    return data[raw+va-base-rva:raw+va-base-rva+size]
            raise ValueError('VA outside raw sections: %08X' % va)

        ranges = [
            ('needle-map', 0x4A0EA0, 0x4A0F4E),
            ('HUD-consumer', 0x4A0F50, 0x4A117B),
            ('engine-gear-update', 0x4757D0, 0x4759FF),
            ('paired-export-tail', 0x474A60, 0x474A83),
            ('integration-and-export-call', 0x474A90, 0x474C64),
            ('separate-synthetic-RPM-path', 0x427A30, 0x427BB0),
        ]
        report = ['speed.exe SHA-256 ' + sha]
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        for name, start, end in ranges:
            blob = read(start, end-start)
            instructions = list(md.disasm(blob, start))
            if sum(i.size for i in instructions) != len(blob):
                raise ValueError('range ends inside instruction: ' + name)
            report.append('\n%s %08X..%08X' % (name, start, end))
            report.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str)
                          for i in instructions)
        for va in (0x6CC938, 0x6CC93C, 0x6CC940, 0x6CC944,
                   0x6CC948, 0x6CC94C, 0x6CC950, 0x6CC80C):
            report.append('float %08X %.9g' % (va, struct.unpack('<f', read(va, 4))[0]))
        (out / 'instructions.txt').write_text('\n'.join(report)+'\n', encoding='ascii')
        bun = (root / 'Global/INGAMEB.BUN').read_bytes()
        if bun[0x16F40C:0x16F410] != b'JDLZ':
            raise ValueError('HUD package moved')
        package = jdlz(bun[0x16F40C:])
        if b'HUD_SingleRace.fng' not in package[:256]:
            raise ValueError('unexpected FEng package')
        report = ['HUD_SingleRace SHA-256 ' + hashlib.sha256(package).hexdigest().upper()]
        for name, kind, at in (('fill', 'SAT', 0x4054), ('red', 'SAT', 0x411C),
                               ('tach', 'SAT', 0x44AC), ('needle', 'SAT', 0x457C),
                               ('speed', 'SAD', 0x42F0), ('gear', 'SAD', 0x4680),
                               ('units', 'SAD', 0x43EC)):
            if package[at:at+4] != kind.encode('ascii') + b'\0':
                raise ValueError('FEng field moved: ' + name)
            position = struct.unpack_from('<3f', package, at+0x20)
            shape = struct.unpack_from('<2f', package, at+0x3c)
            color = struct.unpack_from('<4I', package, at+4)
            report.append('%s %s %04X position/depth %s size-or-scale %s BGRA %s' %
                          (name, kind, at, position, shape, color))
        (out / 'feng.txt').write_text('\n'.join(report)+'\n', encoding='ascii')
    finally:
        after = manifest(root), retail_manifest(root)
        (out / 'after.json').write_bytes(after[0])
        if after != before:
            raise RuntimeError('retail files or directories changed')
    print('PASS: bounded HUD trace and FEng fields; retail files/directories unchanged')


if __name__ == '__main__':
    main()
