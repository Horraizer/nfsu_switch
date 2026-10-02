"""Issue 248 bounded, read-only speed.exe trace. Requires Capstone and cmp.

This prints instructions and runtime-read tables into a fresh scratch directory.
No executable bytes or derived assets are stored in the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
from handling_measure import manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    forbidden = Path('E:/Games').resolve()
    if root == out or root in out.parents or out == forbidden or forbidden in out.parents:
        parser.error('output must be outside retail root and E:/Games')
    cmp = shutil.which('cmp')
    if not cmp:
        parser.error('cmp must be on PATH')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    out.mkdir(parents=True, exist_ok=False)
    (out/'before.json').write_bytes(manifest(root))
    try:
        data = (root/'speed.exe').read_bytes()
        digest = hashlib.sha256(data).hexdigest().upper()
        if digest != 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7':
            raise ValueError('unsupported speed.exe SHA-256')
        # Verify the VA-to-file mapping instead of assuming all PEs use it.
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe+4] != b'PE\0\0':
            raise ValueError('not a PE')
        count = struct.unpack_from('<H', data, pe+6)[0]
        optsize = struct.unpack_from('<H', data, pe+20)[0]
        base = struct.unpack_from('<I', data, pe+24+28)[0]
        sections = []
        for k in range(count):
            at = pe+24+optsize+40*k
            size, va, rawsize, raw = struct.unpack_from('<IIII', data, at+8)
            sections.append((base+va, rawsize, raw))

        def read(va, size):
            for start, length, raw in sections:
                if start <= va and va+size <= start+length:
                    return data[raw+va-start:raw+va-start+size]
            raise ValueError('VA outside raw sections: %08X' % va)

        # Verified instruction boundaries, deliberately not a sweep through
        # compiler padding or a raw displacement match presented as a function.
        ranges = [
            ('physics-construction-call', 0x426224, 0x426235),
            ('physics-pointer-layout', 0x4740D0, 0x47414D),
            ('forward-speed-writer', 0x474730, 0x47485F),
            ('input-to-steering', 0x4615E0, 0x4616B4),
            ('steering-update', 0x460930, 0x46096D),
            ('steering-target-and-slew', 0x460970, 0x460A76),
            ('steering-limit-and-angle-assist', 0x460A80, 0x460DD8),
            ('steering-rate', 0x460DE0, 0x460E66),
            ('steering-constructor', 0x460E70, 0x460F2D),
            ('steering-predicate', 0x4607A0, 0x4607D4),
            ('history-allocation', 0x584180, 0x5841CF),
            ('timed-history-constructor', 0x584360, 0x5843C4),
            ('timed-history-clear', 0x5844B0, 0x5844FA),
            ('timed-history-update', 0x584500, 0x58460E),
            ('predicted-speed-consumer', 0x580240, 0x5802BC),
            ('processed-control-writers', 0x461A60, 0x461D9D),
            ('handbrake-input', 0x461E10, 0x461E58),
            ('angle-difference-helper', 0x464ED0, 0x464F11),
            ('ackermann', 0x588240, 0x5883DC),
            ('lateral-curve-caller', 0x5876F6, 0x587740),
            ('lateral-curve', 0x587740, 0x587822),
            ('static-grip-limit', 0x587830, 0x5878EA),
            ('inertia-and-substeps', 0x474A90, 0x474C99),
            ('inverse-inertias', 0x584B10, 0x584B72),
            ('inertia-selector', 0x585145, 0x585181),
            ('angular-damping', 0x474DD0, 0x474E8B),
            ('tuning-order', 0x575960, 0x575A03),
            ('inertia-rebuild', 0x5762FC, 0x57635B),
            ('engine-coupling', 0x475530, 0x4757C8),
        ]
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        report = ['speed.exe SHA-256 ' + digest]
        for name, start, end in ranges:
            blob = read(start, end-start)
            report.append('\n%s %08X..%08X sha256 %s' %
                          (name, start, end, hashlib.sha256(blob).hexdigest()))
            instructions = list(md.disasm(blob, start))
            if sum(i.size for i in instructions) != len(blob):
                raise ValueError('range does not end on a decoded boundary: ' + name)
            report.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str)
                          for i in instructions)
        curves = []
        for va in (0x705F48, 0x705F5C, 0x705F70, 0x705F84, 0x705F98, 0x705FAC, 0x705F20, 0x705F34):
            n, x0, x1, scale, ptr = struct.unpack('<IfffI', read(va,20))
            if not 1 <= n <= 64:
                raise ValueError('unexpected curve size')
            blob = read(ptr,4*n)
            curves.append(dict(descriptor=hex(va), source=hex(ptr), x0=x0,
                               x1=x1, scale=scale, values=struct.unpack('<'+'f'*n,blob),
                               sha256=hashlib.sha256(blob).hexdigest()))
        # Values are observations, not a built-in replacement table.
        constants = {}
        for va in (0x6FFF10, 0x6B7380, 0x6B7390, 0x6F0868, 0x6F086C,
                   0x6F0870, 0x6CCBB4, 0x6CC9F4, 0x70013C, 0x700140,
                   0x700148, 0x70014C):
            blob = read(va,4)
            constants[hex(va)] = dict(f32=struct.unpack('<f',blob)[0],
                                      u32=struct.unpack('<I',blob)[0])
        global_data = (root/'Global/GLOBALB.BUN').read_bytes()
        global_hash = hashlib.sha256(global_data).hexdigest().upper()
        if global_hash != '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9':
            raise ValueError('unsupported GLOBALB.BUN SHA-256')
        record = global_data[1736752:1736752+0xC90]
        if hashlib.sha256(record).hexdigest().upper() != '212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B':
            raise ValueError('unexpected 240SX record')
        constants['240SX record+0x380'] = dict(f32=struct.unpack_from('<f',record,0x380)[0])
        (out/'instructions.txt').write_text('\n'.join(report)+'\n', encoding='ascii')
        (out/'tables.json').write_text(json.dumps(dict(curves=curves, constants=constants),
                                                indent=2)+'\n', encoding='ascii')
        print('PASS: bounded handling trace; source identity and PE mapping checked')
    finally:
        (out/'after.json').write_bytes(manifest(root))
        subprocess.run([cmp, str(out/'before.json'), str(out/'after.json')], check=True)
        print('PASS: retail manifests identical')


if __name__ == '__main__':
    main()
